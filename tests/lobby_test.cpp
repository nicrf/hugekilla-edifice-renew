// Unit test of the edifice-lobby client (src/net/lobby.cpp, roadmap M7c): the
// readers of the lobby's answers, hostile ones included, and the announcement
// body. With HK_LOBBY_URL=http://127.0.0.1:<port> (a running server/), also the
// whole HTTP exchange of a host and of a client: session, announce, list, lookup
// by game code, renewal, removal (CI runs it this way, see tests/net_lobby.py).
#include "lobby.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <thread>

static int failures;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); failures++; } } while(0)

static int List(const char *json,LobbyGame *games,int max=LOBBY_MAXGAMES)
{
	return LobbyParseList(json,(int)strlen(json),games,max);
}

static void Parsers()
{
	static LobbyGame games[LOBBY_MAXGAMES];
	const char *good="{\"servers\":[{\"id\":\"ABC234\",\"name\":\"Fort \\\"Nico\\\"\",\"address\":\"203.0.113.7\","
		"\"port\":27020,\"version\":\"0.4.0\",\"protocol\":5,\"players\":3,\"maxplayers\":32,\"mode\":1,"
		"\"visibility\":\"public\",\"identity\":\"str:edifice-0011223344556677\",\"extra\":{\"a\":[1,2.5e3,true,null]}},"
		" {\"id\":\"XYZ789\",\"name\":\"Second\",\"address\":\"10.0.0.1\",\"port\":27030,\"protocol\":4}]}";
	CHECK(List(good,games)==2);
	CHECK(!strcmp(games[0].id,"ABC234"));
	CHECK(!strcmp(games[0].name,"Fort \"Nico\""));
	CHECK(!strcmp(games[0].address,"203.0.113.7"));
	CHECK(games[0].port==27020&&games[0].protocol==5&&games[0].players==3&&games[0].maxplayers==32&&games[0].mode==1);
	CHECK(!strcmp(games[0].version,"0.4.0"));
	CHECK(!strcmp(games[0].identity,"str:edifice-0011223344556677"));
	CHECK(!strcmp(games[1].visibility,"public"));   // default
	CHECK(List(good,games,1)==1);                     // capped, still valid
	CHECK(List("{\"servers\":[]}",games)==0);
	CHECK(List("{\"servers\":null}",games)==0);
	CHECK(List(" {} ",games)==0);

	// malformed documents
	const char *bad[]={"","{","{\"servers\":[","{\"servers\":[}]}","[]","{\"servers\":[]} x",
		"{\"servers\":[{\"id\":\"A\"]}","{\"a\":01}","{\"a\":1.}","{\"a\":-}","{\"a\":\"\\x\"}",
		"{\"a\":\"\\u12\"}","{\"a\":\"line\nbreak\"}","{\"a\":tru}","{\"a\" 1}","{\"a\":1,}","{,}",
		"{\"servers\":[{}],}"};
	for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)
	{
		int n=List(bad[i],games);
		if(n!=-1) printf("accepted: %s\n",bad[i]);
		CHECK(n==-1);
	}
	// nesting beyond the limit is refused, not a stack overflow
	char deep[4000];
	int pos=snprintf(deep,sizeof(deep),"{\"x\":");
	for(int i=0;i<1000;i++) deep[pos++]='[';
	for(int i=0;i<1000;i++) deep[pos++]=']';
	strcpy(deep+pos,"}");
	CHECK(List(deep,games)==-1);

	// invalid entries are skipped, the others kept
	const char *mixed="{\"servers\":["
		"{\"id\":\"OK1\",\"name\":\"a\",\"address\":\"1.2.3.4\",\"port\":1,\"protocol\":5},"
		"{\"id\":\"../x\",\"name\":\"a\",\"address\":\"1.2.3.4\",\"port\":1,\"protocol\":5},"       // id
		"{\"id\":\"B\",\"name\":\"a\",\"address\":\"1.2.3.256\",\"port\":1,\"protocol\":5},"         // address
		"{\"id\":\"C\",\"name\":\"a\",\"address\":\"255.255.255.2551\",\"port\":1,\"protocol\":5},"  // address cut
		"{\"id\":\"D\",\"name\":\"a\",\"address\":\"::1\",\"port\":1,\"protocol\":5},"               // IPv6
		"{\"id\":\"E\",\"name\":\"a\",\"address\":\"01.2.3.4\",\"port\":1,\"protocol\":5},"
		"{\"id\":\"F\",\"name\":\"a\",\"address\":\"1.2.3.4\",\"port\":70000,\"protocol\":5},"
		"{\"id\":\"G\",\"name\":\"a\",\"address\":\"1.2.3.4\",\"port\":\"27020\",\"protocol\":5},"   // type
		"{\"id\":\"H\",\"name\":\"a\",\"address\":\"1.2.3.4\",\"port\":1.5,\"protocol\":5},"
		"{\"id\":\"I\",\"name\":\"a\",\"address\":\"1.2.3.4\",\"port\":1,\"protocol\":5,\"players\":-1},"
		"{\"id\":\"J\",\"name\":\"a\",\"address\":\"1.2.3.4\",\"port\":1,\"protocol\":5,\"visibility\":\"secret\"},"
		"{\"id\":\"K\",\"name\":\"\",\"address\":\"1.2.3.4\",\"port\":1,\"protocol\":5},"            // no name
		"{\"id\":\"L\",\"name\":\"a\",\"address\":\"1.2.3.4\",\"port\":1},"                           // no protocol
		"{\"id\":\"OK1\",\"name\":\"dup\",\"address\":\"1.2.3.4\",\"port\":1,\"protocol\":5},"        // same id
		"{\"id\":\"M234567890ABCDEFG\",\"name\":\"a\",\"address\":\"1.2.3.4\",\"port\":1,\"protocol\":5},"  // 17 chars
		"42,\"x\",[1],"
		"{\"id\":\"OK2\",\"name\":\"caf\\u00e9\\u0001\\t<b>\",\"address\":\"5.6.7.8\",\"port\":65535,\"protocol\":0,"
		"\"players\":99999999999999999999999,\"maxplayers\":1e400}"
		"]}";
	int n=List(mixed,games);
	CHECK(n==1);
	CHECK(n>=1&&!strcmp(games[0].id,"OK1")&&!strcmp(games[0].name,"a"));
	// a cleaned name: non-ASCII and control characters dropped; huge numbers rejected
	const char *clean="{\"id\":\"OK2\",\"name\":\"caf\\u00e9\\u0001\\t<b>\",\"address\":\"5.6.7.8\",\"port\":65535,\"protocol\":0}";
	LobbyGame g;
	CHECK(LobbyParseGame(clean,(int)strlen(clean),&g)==1);
	CHECK(!strcmp(g.name,"caf<b>"));
	// a long name is cut, not refused
	char longname[400];
	snprintf(longname,sizeof(longname),"{\"id\":\"A\",\"name\":\"%0200d\",\"address\":\"1.2.3.4\",\"port\":1,\"protocol\":1}",0);
	CHECK(LobbyParseGame(longname,(int)strlen(longname),&g)==1&&strlen(g.name)==64);
	CHECK(LobbyParseGame("{\"id\":\"A\"}",10,&g)==0);
	CHECK(LobbyParseGame(clean,(int)strlen(clean)-1,&g)==0);   // truncated
	CHECK(LobbyParseGame(0,0,&g)==0);

	// announce answer
	char id[LOBBY_CODELEN+1],token[65];
	int refresh;
	const char *ann="{\"id\":\"K7P2QX\",\"refresh_seconds\":20,\"token\":\"00112233445566778899aabbccddeeff\"}";
	CHECK(LobbyParseAnnounced(ann,(int)strlen(ann),id,sizeof(id),token,sizeof(token),&refresh)==1);
	CHECK(!strcmp(id,"K7P2QX")&&!strcmp(token,"00112233445566778899aabbccddeeff")&&refresh==20);
	const char *fast="{\"id\":\"A\",\"token\":\"b\",\"refresh_seconds\":0}";
	CHECK(LobbyParseAnnounced(fast,(int)strlen(fast),id,sizeof(id),token,sizeof(token),&refresh)==1&&refresh==2);
	const char *slow="{\"id\":\"A\",\"token\":\"b\",\"refresh_seconds\":1000000}";
	CHECK(LobbyParseAnnounced(slow,(int)strlen(slow),id,sizeof(id),token,sizeof(token),&refresh)==1&&refresh==300);
	const char *badtoken="{\"id\":\"A\",\"token\":\"b\\r\\nX-Evil: 1\"}";
	CHECK(LobbyParseAnnounced(badtoken,(int)strlen(badtoken),id,sizeof(id),token,sizeof(token),&refresh)==0);
	char longtoken[200];
	snprintf(longtoken,sizeof(longtoken),"{\"id\":\"A\",\"token\":\"%070d\"}",0);
	CHECK(LobbyParseAnnounced(longtoken,(int)strlen(longtoken),id,sizeof(id),token,sizeof(token),&refresh)==0);
	CHECK(LobbyParseAnnounced("{\"id\":\"A\"}",10,id,sizeof(id),token,sizeof(token),&refresh)==0);

	// session, with its STUN/TURN servers and signaling relay (M7d)
	LobbySession session;
	const char *sess="{\"identity\":\"str:edifice-0011223344556677\",\"token\":\"ab01\",\"expires\":\"2026-01-01T00:00:00Z\","
		"\"ice\":{\"stun\":[\"stun:x:3478\",\"stun:a,b:1\",7],\"turn\":[{\"url\":\"turn:x:3478?transport=udp\",\"username\":\"1:str:e\","
		"\"credential\":\"c+/=\"},{\"url\":\"turn:y\",\"username\":\"u v\",\"credential\":\"c\"},{\"url\":\"turn:z\"}]},"
		"\"signaling\":\"lobby.example.org:10000\",\"signaling_url\":\"wss://lobby.example.org/v1/signal\"}";
	CHECK(LobbyParseSession(sess,(int)strlen(sess),&session)==1);
	CHECK(!strcmp(session.signalurl,"wss://lobby.example.org/v1/signal"));
	CHECK(!strcmp(session.identity,"str:edifice-0011223344556677")&&!strcmp(session.token,"ab01"));
	CHECK(session.numstun==1&&!strcmp(session.stun[0],"stun:x:3478"));   // a comma, a number: skipped
	CHECK(session.numturn==1&&!strcmp(session.turn[0].url,"turn:x:3478?transport=udp")&&
		!strcmp(session.turn[0].username,"1:str:e")&&!strcmp(session.turn[0].credential,"c+/="));
	CHECK(!strcmp(session.signaling,"lobby.example.org:10000"));
	const char *nosig="{\"identity\":\"str:edifice-1\",\"token\":\"ab\",\"signaling\":\"a b:1\",\"ice\":5}";
	CHECK(LobbyParseSession(nosig,(int)strlen(nosig),&session)==1&&!session.signaling[0]&&!session.numstun);
	const char *badurl="{\"identity\":\"str:edifice-1\",\"token\":\"ab\",\"signaling_url\":\"https://x/v1/signal\"}";
	CHECK(LobbyParseSession(badurl,(int)strlen(badurl),&session)==1&&!session.signalurl[0]);
	const char *spaced="{\"identity\":\"a b\",\"token\":\"ab\"}";
	CHECK(LobbyParseSession(spaced,(int)strlen(spaced),&session)==0&&!session.identity[0]&&!session.token[0]);
	char many[2000];
	int at=snprintf(many,sizeof(many),"{\"identity\":\"str:e\",\"token\":\"ab\",\"ice\":{\"stun\":[");
	for(int i=0;i<10;i++) at+=snprintf(many+at,sizeof(many)-at,"%s\"stun:h%d:1\"",i?",":"",i);
	snprintf(many+at,sizeof(many)-at,"]}}");
	CHECK(LobbyParseSession(many,(int)strlen(many),&session)==1&&session.numstun==LOBBY_MAXICE);

	// the announcement carries the session token beside the identity
	LobbyAnnouncement withid={"Fort",27020,"0.4.0",5,1,32,0,"public","str:edifice-1","ab01"};
	char withbody[400];
	CHECK(LobbyAnnouncementJSON(&withid,withbody,sizeof(withbody))&&
		strstr(withbody,"\"identity\":\"str:edifice-1\",\"session\":\"ab01\""));
	CHECK(!strstr(withbody,"lan"));   // no LAN address: no field (what older games send)
	CHECK(!strstr(withbody,"webrtc"));

	// "webrtc" (API 4, M14 task 3): written only with an identity, read as an extra
	LobbyAnnouncement withrtc=withid;
	withrtc.webrtc=1;
	CHECK(LobbyAnnouncementJSON(&withrtc,withbody,sizeof(withbody))&&
		strstr(withbody,"\"session\":\"ab01\",\"webrtc\":true,\"port\""));
	withrtc.identity="";
	CHECK(LobbyAnnouncementJSON(&withrtc,withbody,sizeof(withbody))&&!strstr(withbody,"webrtc"));
	const char *rtc="{\"id\":\"W1\",\"name\":\"n\",\"address\":\"203.0.113.7\",\"port\":27020,\"protocol\":5,"
		"\"identity\":\"str:edifice-1\",\"webrtc\":true}";
	CHECK(LobbyParseGame(rtc,(int)strlen(rtc),&g)==1&&g.webrtc==1);
	const char *rtcnoid="{\"id\":\"W2\",\"name\":\"n\",\"address\":\"203.0.113.7\",\"port\":27020,\"protocol\":5,\"webrtc\":true}";
	CHECK(LobbyParseGame(rtcnoid,(int)strlen(rtcnoid),&g)==1&&g.webrtc==0);
	const char *rtcodd="{\"id\":\"W3\",\"name\":\"n\",\"address\":\"203.0.113.7\",\"port\":27020,\"protocol\":5,"
		"\"identity\":\"str:edifice-1\",\"webrtc\":\"yes\"}";
	CHECK(LobbyParseGame(rtcodd,(int)strlen(rtcodd),&g)==1&&g.webrtc==0);
	const char *rtcfalse="{\"id\":\"W4\",\"name\":\"n\",\"address\":\"203.0.113.7\",\"port\":27020,\"protocol\":5,"
		"\"identity\":\"str:edifice-1\",\"webrtc\":false}";
	CHECK(LobbyParseGame(rtcfalse,(int)strlen(rtcfalse),&g)==1&&g.webrtc==0);

	// LAN addresses (same network): only private IPv4 written and read, never a reason
	// to drop the game
	CHECK(LobbyLANAddress("10.0.0.1")&&LobbyLANAddress("172.16.0.1")&&LobbyLANAddress("172.31.9.9")&&
		LobbyLANAddress("192.168.1.20"));
	CHECK(!LobbyLANAddress("172.32.0.1")&&!LobbyLANAddress("8.8.8.8")&&!LobbyLANAddress("127.0.0.1")&&
		!LobbyLANAddress("192.169.0.1")&&!LobbyLANAddress("192.168.1")&&!LobbyLANAddress("010.0.0.1")&&
		!LobbyLANAddress("")&&!LobbyLANAddress(0)&&!LobbyLANAddress("100.64.0.1"));
	LobbyAnnouncement withlan=withid;
	withlan.numlan=3;
	withlan.lan[0]="192.168.1.20";
	withlan.lan[1]="8.8.8.8";
	withlan.lan[2]="10.0.0.5";
	char lanbody[500];
	CHECK(LobbyAnnouncementJSON(&withlan,lanbody,sizeof(lanbody))&&
		strstr(lanbody,",\"lan\":[\"192.168.1.20\",\"10.0.0.5\"],\"port\":27020"));
	const char *lan="{\"id\":\"L1\",\"name\":\"n\",\"address\":\"203.0.113.7\",\"port\":27020,\"protocol\":5,"
		"\"lan\":[\"192.168.1.20\",\"1.2.3.4\",7,{\"a\":1},\"192.168.1.20\",\"10.0.0.5\",\"10.0.0.0.1\","
		"\"172.16.0.1\",\"172.16.0.2\",\"172.16.0.3\"]}";
	CHECK(LobbyParseGame(lan,(int)strlen(lan),&g)==1&&g.numlan==LOBBY_MAXLAN&&!strcmp(g.lan[0],"192.168.1.20")&&
		!strcmp(g.lan[1],"10.0.0.5")&&!strcmp(g.lan[2],"172.16.0.1")&&!strcmp(g.lan[3],"172.16.0.2"));
	const char *lanbad="{\"id\":\"L2\",\"name\":\"n\",\"address\":\"203.0.113.7\",\"port\":27020,\"protocol\":5,\"lan\":\"10.0.0.1\"}";
	CHECK(LobbyParseGame(lanbad,(int)strlen(lanbad),&g)==1&&g.numlan==0);
	const char *nolan="{\"id\":\"L3\",\"name\":\"n\",\"address\":\"203.0.113.7\",\"port\":27020,\"protocol\":5}";
	CHECK(LobbyParseGame(nolan,(int)strlen(nolan),&g)==1&&g.numlan==0);

	// error
	char msg[40];
	const char *err="{\"error\":\"too many servers\"}";
	CHECK(LobbyParseError(err,(int)strlen(err),msg,sizeof(msg))==1&&!strcmp(msg,"too many servers"));
	CHECK(LobbyParseError("<html>",6,msg,sizeof(msg))==0&&!msg[0]);

	// game codes
	char code[LOBBY_CODELEN+1];
	CHECK(LobbyNormalizeCode(" k7p-2qx",code,sizeof(code))==1&&!strcmp(code,"K7P2QX"));
	CHECK(LobbyNormalizeCode("",code,sizeof(code))==0);
	CHECK(LobbyNormalizeCode("ab/cd",code,sizeof(code))==0);
	CHECK(LobbyNormalizeCode("ab?x=1",code,sizeof(code))==0);
	CHECK(LobbyNormalizeCode("0123456789ABCDEFG",code,sizeof(code))==0);   // 17
	CHECK(LobbyNormalizeCode("ABCD",code,3)==0);                             // doesn't fit

	// announcement body: escaped, printable ASCII only, and readable back as a game
	LobbyAnnouncement a={"My \"fort\"\\\n\xc3\xa9",27020,"0.4.0",5,2,32,0,"private",""};
	char body[400];
	CHECK(LobbyAnnouncementJSON(&a,body,sizeof(body))==1);
	CHECK(!strcmp(body,"{\"name\":\"My \\\"fort\\\"\\\\\",\"version\":\"0.4.0\",\"visibility\":\"private\",\"identity\":\"\",\"session\":\"\","
		"\"port\":27020,\"protocol\":5,\"players\":2,\"maxplayers\":32,\"mode\":0}"));
	char asgame[500];
	snprintf(asgame,sizeof(asgame),"{\"id\":\"Z\",\"address\":\"1.1.1.1\",%s",body+1);
	CHECK(LobbyParseGame(asgame,(int)strlen(asgame),&g)==1&&!strcmp(g.name,"My \"fort\"\\")&&!strcmp(g.visibility,"private"));
	int bodylen=(int)strlen(body);
	for(int size=0;size<=bodylen;size++)   // too small: refused, never overflows
	{
		char *exact=new char[size?size:1];   // heap: ASan sees one byte too far
		CHECK(LobbyAnnouncementJSON(&a,exact,size)==0);
		delete []exact;
	}

	// URL
	CHECK(LobbySetURL("ftp://x",0)==0&&!LobbyGetURL()[0]);
	CHECK(LobbySetURL("https://a b",0)==0);
	LobbySetURL("https://lobby.example.org/",0);
	CHECK(!strcmp(LobbyGetURL(),"https://lobby.example.org"));
	LobbySetURL("",0);
	CHECK(LobbySend("GET","/v1/servers",0,0)==0);   // not configured

	// mutations of the valid documents: whatever the bytes, no crash (ASan/UBSan
	// builds and tests/fuzz/fuzz_lobby.cpp go further)
	unsigned seed=12345;
	const char *docs[]={good,mixed,ann,sess,err};
	for(int round=0;round<20000;round++)
	{
		const char *src=docs[round%5];
		int len=(int)strlen(src);
		char buf[4096];
		memcpy(buf,src,len);
		for(int k=0;k<1+round%4;k++)
		{
			seed=seed*1103515245u+12345u;
			int at=(seed>>8)%len;
			seed=seed*1103515245u+12345u;
			buf[at]=(char)(seed>>16);
		}
		seed=seed*1103515245u+12345u;
		int cut=round%3?len:(int)((seed>>8)%(len+1));
		LobbyParseList(buf,cut,games,LOBBY_MAXGAMES);
		LobbyParseGame(buf,cut,&g);
		LobbyParseAnnounced(buf,cut,id,sizeof(id),token,sizeof(token),&refresh);
		LobbyParseSession(buf,cut,&session);
		LobbyParseError(buf,cut,msg,sizeof(msg));
	}
}

//---- against a running lobby ----

static int Wait(int request,int *status,std::string *reply)
{
	for(int i=0;i<1500;i++)
	{
		if(LobbyPoll(request,status,reply)) return 1;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return 0;
}

static int Request(const char *method,const char *path,const char *body,const char *token,std::string *reply)
{
	int status=-1;
	int r=LobbySend(method,path,body,token);
	CHECK(r>0);
	CHECK(Wait(r,&status,reply));
	printf("%s %s -> %d\n",method,path,status);
	return status;
}

static void Online(const char *url)
{
	CHECK(LobbySetURL(url,"Edifice-test"));
	std::string reply;
	char identity[65],token[65],id[LOBBY_CODELEN+1],privid[LOBBY_CODELEN+1],privtoken[65];
	int refresh;
	CHECK(Request("POST","/v1/sessions",0,0,&reply)==201);
	LobbySession session;
	CHECK(LobbyParseSession(reply.data(),(int)reply.size(),&session));
	snprintf(identity,sizeof(identity),"%s",session.identity);

	char body[600];
	LobbyAnnouncement pub={"lobby_test public",27999,"test",9999,1,8,1,"public",identity,session.token};
	LobbyAnnouncementJSON(&pub,body,sizeof(body));
	CHECK(Request("POST","/v1/servers",body,0,&reply)==201);
	CHECK(LobbyParseAnnounced(reply.data(),(int)reply.size(),id,sizeof(id),token,sizeof(token),&refresh));
	LobbyAnnouncement priv=pub;
	priv.name="lobby_test private";
	priv.visibility="private";
	priv.numlan=1;
	priv.lan[0]="192.168.1.20";   // given back to us: same address as the host (127.0.0.1)
	LobbyAnnouncementJSON(&priv,body,sizeof(body));
	CHECK(Request("POST","/v1/servers",body,0,&reply)==201);
	CHECK(LobbyParseAnnounced(reply.data(),(int)reply.size(),privid,sizeof(privid),privtoken,sizeof(privtoken),&refresh));
	CHECK(strlen(id)==6&&strlen(privid)==6);

	// the list has the public game only
	static LobbyGame games[LOBBY_MAXGAMES];
	CHECK(Request("GET","/v1/servers?protocol=9999",0,0,&reply)==200);
	int n=LobbyParseList(reply.data(),(int)reply.size(),games,LOBBY_MAXGAMES);
	CHECK(n==1);
	CHECK(n==1&&!strcmp(games[0].id,id)&&!strcmp(games[0].name,"lobby_test public")&&games[0].port==27999&&
		games[0].players==1&&games[0].maxplayers==8&&games[0].mode==1&&!strcmp(games[0].address,"127.0.0.1")&&
		!strcmp(games[0].identity,identity));

	// the private game by its code, typed in lower case
	char path[100],code[LOBBY_CODELEN+1];
	snprintf(code,sizeof(code),"%s",privid);
	for(char *c=code;*c;c++) if(*c>='A'&&*c<='Z') *c+=32;
	snprintf(path,sizeof(path),"/v1/servers/%s",code);
	LobbyGame g;
	CHECK(Request("GET",path,0,0,&reply)==200);
	CHECK(LobbyParseGame(reply.data(),(int)reply.size(),&g)&&!strcmp(g.id,privid)&&!strcmp(g.visibility,"private")&&
		g.numlan==1&&!strcmp(g.lan[0],"192.168.1.20"));

	// renewal with new details
	pub.players=4;
	LobbyAnnouncementJSON(&pub,body,sizeof(body));
	snprintf(path,sizeof(path),"/v1/servers/%s",id);
	CHECK(Request("PUT",path,body,token,&reply)==204);
	CHECK(Request("GET",path,0,0,&reply)==200);
	CHECK(LobbyParseGame(reply.data(),(int)reply.size(),&g)&&g.players==4);

	// removal: a wrong token is refused with a readable error
	char msg[100];
	CHECK(Request("DELETE",path,0,"wrong",&reply)==403);
	CHECK(LobbyParseError(reply.data(),(int)reply.size(),msg,sizeof(msg))&&!strcmp(msg,"bad token"));
	CHECK(Request("DELETE",path,0,token,&reply)==204);
	CHECK(Request("GET",path,0,0,&reply)==404);
	snprintf(path,sizeof(path),"/v1/servers/%s",privid);
	CHECK(Request("DELETE",path,0,privtoken,&reply)==204);

	// shutdown waits for a queued DELETE (a host quitting)
	LobbyAnnouncementJSON(&pub,body,sizeof(body));
	CHECK(Request("POST","/v1/servers",body,0,&reply)==201);
	CHECK(LobbyParseAnnounced(reply.data(),(int)reply.size(),id,sizeof(id),token,sizeof(token),&refresh));
	snprintf(path,sizeof(path),"/v1/servers/%s",id);
	LobbyDrop(LobbySend("DELETE",path,0,token));
	LobbyShutdown(5000);
	CHECK(Request("GET",path,0,0,&reply)==404);   // the worker starts again
	LobbyShutdown(1000);

	// nothing listening: an error, quickly, not a hang
	LobbySetURL("http://127.0.0.1:1",0);
	int status=-1;
	int r=LobbySend("GET","/v1/servers",0,0);
	CHECK(Wait(r,&status,&reply)&&status==0&&!reply.empty());
	printf("unreachable lobby: %s\n",reply.c_str());
	LobbyShutdown(0);
}

int main()
{
	Parsers();
	const char *url=getenv("HK_LOBBY_URL");
	if(url&&*url)
	{
		if(LobbyHttpAvailable()) Online(url);
		else { printf("FAIL: HK_LOBBY_URL set but built without libcurl\n"); failures++; }
	}
	else
		printf("HK_LOBBY_URL not set: parsers only\n");
	if(failures) printf("%d failure(s)\n",failures);
	else printf("lobby: all tests passed\n");
	return failures?1:0;
}
