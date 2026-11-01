//edifice-lobby client: HTTP worker thread (libcurl) and the readers of the
//lobby's JSON answers. See lobby.h.

#include "lobby.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//---- JSON reader ----
//Just what the lobby's answers need: objects, arrays, strings, integers; any
//other value is skipped. Bounded everywhere: nesting depth, string and key
//lengths (cut), numbers (clamped); a syntax error makes the whole document invalid.

#define JSON_MAXDEPTH 16

struct JsonIn
{
	const char *p,*end;
	int ok;
};

static void Ws(JsonIn &j)
{
	while(j.p<j.end&&(*j.p==' '||*j.p=='\t'||*j.p=='\n'||*j.p=='\r')) j.p++;
}

static int Peek(JsonIn &j)
{
	Ws(j);
	return j.ok&&j.p<j.end?(unsigned char)*j.p:-1;
}

static int Fail(JsonIn &j)
{
	j.ok=0;
	return 0;
}

static int Expect(JsonIn &j,char c)
{
	if(Peek(j)!=(unsigned char)c) return Fail(j);
	j.p++;
	return 1;
}

static int Hex(int c)
{
	if(c>='0'&&c<='9') return c-'0';
	if(c>='a'&&c<='f') return c-'a'+10;
	if(c>='A'&&c<='F') return c-'A'+10;
	return -1;
}

//a string; keeps its printable ASCII characters (the game font) in out, cut to
//size-1. out may be 0 (skip). Returns 1 if it was cut.
static int String(JsonIn &j,char *out,int size)
{
	int n=0,cut=0;
	if(out&&size>0) out[0]=0;
	if(!Expect(j,'"')) return 0;
	while(1)
	{
		if(j.p>=j.end) return Fail(j);
		int c=(unsigned char)*j.p++;
		if(c=='"') break;
		if(c<0x20) return Fail(j);
		if(c=='\\')
		{
			if(j.p>=j.end) return Fail(j);
			c=(unsigned char)*j.p++;
			switch(c)
			{
			case '"': case '\\': case '/': break;
			case 'b': case 'f': case 'n': case 'r': case 't': c=0; break;
			case 'u':
				{
					if(j.end-j.p<4) return Fail(j);
					int v=0;
					for(int i=0;i<4;i++)
					{
						int h=Hex((unsigned char)j.p[i]);
						if(h<0) return Fail(j);
						v=v*16+h;
					}
					j.p+=4;
					c=v;
				}
				break;
			default: return Fail(j);
			}
		}
		if(c<32||c>126) continue;   //control, non-ASCII: dropped
		if(!out) continue;
		if(n<size-1) out[n++]=(char)c;
		else cut=1;
	}
	if(out&&size>0) out[n]=0;
	return cut;
}

//a number; *isint when it is a plain integer, its value clamped to +-1e15
static void Number(JsonIn &j,long long *value,int *isint)
{
	*value=0;
	*isint=1;
	Ws(j);
	int neg=0;
	if(j.p<j.end&&*j.p=='-') { neg=1; j.p++; }
	if(j.p>=j.end||*j.p<'0'||*j.p>'9') { Fail(j); return; }
	if(*j.p=='0'&&j.p+1<j.end&&j.p[1]>='0'&&j.p[1]<='9') { Fail(j); return; }
	while(j.p<j.end&&*j.p>='0'&&*j.p<='9')
	{
		if(*value<1000000000000000LL) *value=*value*10+(*j.p-'0');
		j.p++;
	}
	if(j.p<j.end&&*j.p=='.')
	{
		*isint=0;
		j.p++;
		if(j.p>=j.end||*j.p<'0'||*j.p>'9') { Fail(j); return; }
		while(j.p<j.end&&*j.p>='0'&&*j.p<='9') j.p++;
	}
	if(j.p<j.end&&(*j.p=='e'||*j.p=='E'))
	{
		*isint=0;
		j.p++;
		if(j.p<j.end&&(*j.p=='+'||*j.p=='-')) j.p++;
		if(j.p>=j.end||*j.p<'0'||*j.p>'9') { Fail(j); return; }
		while(j.p<j.end&&*j.p>='0'&&*j.p<='9') j.p++;
	}
	if(neg) *value=-*value;
}

static int Literal(JsonIn &j,const char *word)
{
	Ws(j);
	int n=strlen(word);
	if(j.end-j.p<n||memcmp(j.p,word,n)) return Fail(j);
	j.p+=n;
	return 1;
}

static int ObjectNext(JsonIn &j,int &first,char *key,int keysize);
static int ArrayNext(JsonIn &j,int &first);

//any value
static void Skip(JsonIn &j,int depth)
{
	if(depth>JSON_MAXDEPTH) { Fail(j); return; }
	int c=Peek(j);
	if(c=='"') String(j,0,0);
	else if(c=='{')
	{
		j.p++;
		int first=1;
		char key[2];
		while(ObjectNext(j,first,key,sizeof(key))) Skip(j,depth+1);
	}
	else if(c=='[')
	{
		j.p++;
		int first=1;
		while(ArrayNext(j,first)) Skip(j,depth+1);
	}
	else if(c=='t') Literal(j,"true");
	else if(c=='f') Literal(j,"false");
	else if(c=='n') Literal(j,"null");
	else
	{
		long long v;
		int isint;
		Number(j,&v,&isint);
	}
}

//after the '{': reads the next key and its ':' (key "" when too long, so it
//matches nothing). Returns 0 at the '}' or on an error.
static int ObjectNext(JsonIn &j,int &first,char *key,int keysize)
{
	int c=Peek(j);
	if(c=='}') { j.p++; return 0; }
	if(!first&&!Expect(j,',')) return 0;
	first=0;
	if(String(j,key,keysize)) key[0]=0;
	if(!j.ok||!Expect(j,':')) return 0;
	return 1;
}

//after the '[': 1 when a value follows
static int ArrayNext(JsonIn &j,int &first)
{
	int c=Peek(j);
	if(c==']') { j.p++; return 0; }
	if(c<0) return Fail(j);
	if(!first&&!Expect(j,',')) return 0;
	first=0;
	return j.ok;
}

//typed values: *valid is cleared when the value has another type (it is skipped),
//or for an exact string (id, address, token) when it doesn't fit
static void StrValue(JsonIn &j,char *out,int size,int *valid,int exact=0)
{
	if(Peek(j)=='"') { if(String(j,out,size)&&exact) *valid=0; }
	else { Skip(j,1); *valid=0; }
}

static long long IntValue(JsonIn &j,int *valid)
{
	int c=Peek(j);
	if(c=='-'||(c>='0'&&c<='9'))
	{
		long long v;
		int isint;
		Number(j,&v,&isint);
		if(!isint) *valid=0;
		return v;
	}
	Skip(j,1);
	*valid=0;
	return 0;
}

static int End(JsonIn &j)
{
	Ws(j);
	return j.ok&&j.p==j.end;
}

static int Alnum(const char *s,int maxlen)
{
	int n=0;
	for(;s[n];n++)
		if(!((s[n]>='0'&&s[n]<='9')||(s[n]>='a'&&s[n]<='z')||(s[n]>='A'&&s[n]<='Z')))
			return 0;
	return n>0&&n<=maxlen;
}

//dotted IPv4, each part 0-255 without leading zeros
static int IPv4(const char *s)
{
	for(int part=0;part<4;part++)
	{
		if(part&&*s++!='.') return 0;
		int v=0,digits=0;
		while(*s>='0'&&*s<='9'&&digits<4) { v=v*10+(*s++-'0'); digits++; }
		if(!digits||digits>3||v>255||(digits>1&&s[-digits]=='0')) return 0;
	}
	return *s==0;
}

int LobbyLANAddress(const char *s)
{
	if(!s||!IPv4(s)) return 0;
	int a=atoi(s),b=atoi(strchr(s,'.')+1);
	return a==10||(a==172&&b>=16&&b<=31)||(a==192&&b==168);
}

//"lan":[...] of a game: its valid LAN addresses, up to LOBBY_MAXLAN; anything else
//is skipped (never makes the game invalid: an extra the game can do without)
static void LanList(JsonIn &j,LobbyGame *g,int depth)
{
	if(Peek(j)!='[') { Skip(j,depth); return; }
	j.p++;
	int first=1;
	while(ArrayNext(j,first))
	{
		if(Peek(j)!='"') { Skip(j,depth+1); continue; }
		char addr[16];
		if(String(j,addr,sizeof(addr))||!LobbyLANAddress(addr)||g->numlan>=LOBBY_MAXLAN) continue;
		int dup=0;
		for(int i=0;i<g->numlan;i++) dup|=!strcmp(g->lan[i],addr);
		if(!dup) strcpy(g->lan[g->numlan++],addr);
	}
}

//the fields of one game object; 1 if it is a game we can show and join
static int GameObject(JsonIn &j,LobbyGame *g,int depth)
{
	memset(g,0,sizeof(*g));
	if(!Expect(j,'{')) return 0;
	int valid=1,first=1;
	int have=0;   //id, name, address, port, protocol
	long long v;
	char key[16];
	while(ObjectNext(j,first,key,sizeof(key)))
	{
		if(!strcmp(key,"id")) { StrValue(j,g->id,sizeof(g->id),&valid,1); have|=1; }
		else if(!strcmp(key,"name")) { StrValue(j,g->name,sizeof(g->name),&valid); have|=2; }
		else if(!strcmp(key,"address")) { StrValue(j,g->address,sizeof(g->address),&valid,1); have|=4; }
		else if(!strcmp(key,"version")) StrValue(j,g->version,sizeof(g->version),&valid);
		else if(!strcmp(key,"visibility")) StrValue(j,g->visibility,sizeof(g->visibility),&valid,1);
		else if(!strcmp(key,"identity")) StrValue(j,g->identity,sizeof(g->identity),&valid,1);
		else if(!strcmp(key,"lan")) LanList(j,g,depth+1);
		else if(!strcmp(key,"port"))
		{
			v=IntValue(j,&valid);
			if(v<1||v>65535) valid=0;
			g->port=(int)v;
			have|=8;
		}
		else if(!strcmp(key,"protocol"))
		{
			v=IntValue(j,&valid);
			if(v<0||v>65535) valid=0;
			g->protocol=(int)v;
			have|=16;
		}
		else if(!strcmp(key,"players"))
		{
			v=IntValue(j,&valid);
			if(v<0||v>255) valid=0;
			g->players=(int)v;
		}
		else if(!strcmp(key,"maxplayers"))
		{
			v=IntValue(j,&valid);
			if(v<0||v>255) valid=0;
			g->maxplayers=(int)v;
		}
		else if(!strcmp(key,"mode"))
		{
			v=IntValue(j,&valid);
			if(v<0||v>255) valid=0;
			g->mode=(int)v;
		}
		else Skip(j,depth+1);
	}
	if(!j.ok) return 0;
	if(!g->visibility[0]) strcpy(g->visibility,"public");
	if(strcmp(g->visibility,"public")&&strcmp(g->visibility,"friends")&&strcmp(g->visibility,"private"))
		valid=0;
	if(strchr(g->identity,' ')) g->identity[0]=0;
	return valid&&have==31&&Alnum(g->id,LOBBY_CODELEN)&&g->name[0]&&IPv4(g->address);
}

int LobbyParseList(const char *json,int len,LobbyGame *games,int max)
{
	if(!json||len<0) return -1;
	JsonIn j={json,json+len,1};
	int count=0,first=1;
	char key[16];
	if(!Expect(j,'{')) return -1;
	while(ObjectNext(j,first,key,sizeof(key)))
	{
		if(strcmp(key,"servers")||Peek(j)!='[')
		{
			Skip(j,1);
			continue;
		}
		j.p++;
		int item=1;
		while(ArrayNext(j,item))
		{
			LobbyGame g;
			if(Peek(j)!='{') { Skip(j,2); continue; }
			if(GameObject(j,&g,2)&&count<max)
			{
				//the same id twice: keep the first
				int dup=0;
				for(int i=0;i<count;i++) dup|=!strcmp(games[i].id,g.id);
				if(!dup) games[count++]=g;
			}
		}
	}
	return End(j)?count:-1;
}

int LobbyParseGame(const char *json,int len,LobbyGame *game)
{
	if(!json||len<0) return 0;
	JsonIn j={json,json+len,1};
	int valid=GameObject(j,game,0);
	return End(j)&&valid;
}

int LobbyParseAnnounced(const char *json,int len,char *id,int idsize,char *token,int tokensize,int *refresh)
{
	if(!json||len<0) return 0;
	JsonIn j={json,json+len,1};
	int valid=1,first=1,have=0;
	char key[20];
	id[0]=token[0]=0;
	*refresh=20;
	if(!Expect(j,'{')) return 0;
	while(ObjectNext(j,first,key,sizeof(key)))
	{
		if(!strcmp(key,"id")) { StrValue(j,id,idsize,&valid,1); have|=1; }
		else if(!strcmp(key,"token")) { StrValue(j,token,tokensize,&valid,1); have|=2; }
		else if(!strcmp(key,"refresh_seconds"))
		{
			long long v=IntValue(j,&valid);
			*refresh=v<2?2:v>300?300:(int)v;
		}
		else Skip(j,1);
	}
	return End(j)&&valid&&have==3&&Alnum(id,LOBBY_CODELEN)&&Alnum(token,tokensize-1);
}

//a text that can go in GameNetworkingSockets' comma-separated server lists
static int ListItem(const char *s)
{
	return s[0]&&!strchr(s,' ')&&!strchr(s,',');
}

//"stun":[...] of a session: the valid URLs, up to LOBBY_MAXICE
static void StunList(JsonIn &j,LobbySession *s)
{
	if(Peek(j)!='[') { Skip(j,2); return; }
	j.p++;
	int first=1;
	while(ArrayNext(j,first))
	{
		int valid=1;
		char url[128];
		StrValue(j,url,sizeof(url),&valid,1);
		if(valid&&ListItem(url)&&s->numstun<LOBBY_MAXICE)
			strcpy(s->stun[s->numstun++],url);
	}
}

//"turn":[{url, username, credential}...]
static void TurnList(JsonIn &j,LobbySession *s)
{
	if(Peek(j)!='[') { Skip(j,2); return; }
	j.p++;
	int item=1;
	while(ArrayNext(j,item))
	{
		if(Peek(j)!='{') { Skip(j,3); continue; }
		j.p++;
		LobbyIceServer t;
		memset(&t,0,sizeof(t));
		int valid=1,first=1,have=0;
		char key[16];
		while(ObjectNext(j,first,key,sizeof(key)))
		{
			if(!strcmp(key,"url")) { StrValue(j,t.url,sizeof(t.url),&valid,1); have|=1; }
			else if(!strcmp(key,"username")) { StrValue(j,t.username,sizeof(t.username),&valid,1); have|=2; }
			else if(!strcmp(key,"credential")) { StrValue(j,t.credential,sizeof(t.credential),&valid,1); have|=4; }
			else Skip(j,4);
		}
		if(valid&&have==7&&ListItem(t.url)&&ListItem(t.username)&&ListItem(t.credential)&&s->numturn<LOBBY_MAXICE)
			s->turn[s->numturn++]=t;
	}
}

int LobbyParseSession(const char *json,int len,LobbySession *s)
{
	memset(s,0,sizeof(*s));
	if(!json||len<0) return 0;
	JsonIn j={json,json+len,1};
	int valid=1,first=1,have=0;
	char key[16];
	if(!Expect(j,'{')) return 0;
	while(ObjectNext(j,first,key,sizeof(key)))
	{
		if(!strcmp(key,"identity")) { StrValue(j,s->identity,sizeof(s->identity),&valid,1); have|=1; }
		else if(!strcmp(key,"token")) { StrValue(j,s->token,sizeof(s->token),&valid,1); have|=2; }
		else if(!strcmp(key,"signaling"))
		{
			int ok=1;
			StrValue(j,s->signaling,sizeof(s->signaling),&ok,1);
			if(!ok||!ListItem(s->signaling)) s->signaling[0]=0;
		}
		else if(!strcmp(key,"signaling_url"))
		{
			int ok=1;
			StrValue(j,s->signalurl,sizeof(s->signalurl),&ok,1);
			if(!ok||!ListItem(s->signalurl)||(strncmp(s->signalurl,"wss://",6)&&strncmp(s->signalurl,"ws://",5)))
				s->signalurl[0]=0;
		}
		else if(!strcmp(key,"ice")&&Peek(j)=='{')
		{
			j.p++;
			int ifirst=1;
			char ikey[8];
			while(ObjectNext(j,ifirst,ikey,sizeof(ikey)))
			{
				if(!strcmp(ikey,"stun")) StunList(j,s);
				else if(!strcmp(ikey,"turn")) TurnList(j,s);
				else Skip(j,2);
			}
		}
		else Skip(j,1);
	}
	int ok=End(j)&&valid&&have==3&&s->identity[0]&&!strchr(s->identity,' ')&&Alnum(s->token,sizeof(s->token)-1);
	if(!ok) memset(s,0,sizeof(*s));
	return ok;
}

int LobbyParseError(const char *json,int len,char *msg,int size)
{
	msg[0]=0;
	if(!json||len<0) return 0;
	JsonIn j={json,json+len,1};
	int valid=1,first=1;
	char key[8];
	if(!Expect(j,'{')) return 0;
	while(ObjectNext(j,first,key,sizeof(key)))
	{
		if(!strcmp(key,"error")) StrValue(j,msg,size,&valid);
		else Skip(j,1);
	}
	if(!End(j)||!valid) msg[0]=0;
	return msg[0]!=0;
}

int LobbyNormalizeCode(const char *in,char *out,int size)
{
	int n=0;
	out[0]=0;
	for(;*in;in++)
	{
		char c=*in;
		if(c=='-'||c==' ') continue;
		if(c>='a'&&c<='z') c-=32;
		if(!((c>='0'&&c<='9')||(c>='A'&&c<='Z'))) return 0;
		if(n>=size-1||n>=LOBBY_CODELEN) return 0;
		out[n++]=c;
	}
	out[n]=0;
	return n>0;
}

//a JSON string of printable ASCII (the lobby keeps nothing else)
static int PutString(char *out,int size,int &pos,const char *s)
{
	if(pos>=size-1) return 0;
	out[pos++]='"';
	for(;s&&*s;s++)
	{
		unsigned char c=*s;
		if(c<32||c>126) continue;
		if(c=='"'||c=='\\')
		{
			if(pos>=size-2) return 0;
			out[pos++]='\\';
		}
		if(pos>=size-1) return 0;
		out[pos++]=c;
	}
	if(pos>=size-1) return 0;
	out[pos++]='"';
	out[pos]=0;
	return 1;
}

static int Put(char *out,int size,int &pos,const char *text)
{
	int n=strlen(text);
	if(pos+n>=size) return 0;
	memcpy(out+pos,text,n+1);
	pos+=n;
	return 1;
}

int LobbyAnnouncementJSON(const LobbyAnnouncement *a,char *out,int size)
{
	int pos=0;
	char num[160];
	if(size<1) return 0;
	out[0]=0;
	int ok=Put(out,size,pos,"{\"name\":")&&PutString(out,size,pos,a->name)&&
		Put(out,size,pos,",\"version\":")&&PutString(out,size,pos,a->version)&&
		Put(out,size,pos,",\"visibility\":")&&PutString(out,size,pos,a->visibility)&&
		Put(out,size,pos,",\"identity\":")&&PutString(out,size,pos,a->identity)&&
		Put(out,size,pos,",\"session\":")&&PutString(out,size,pos,a->session);
	if(ok&&a->numlan>0)
	{
		//only private IPv4 (the lobby drops anything else too); old lobbies ignore it
		ok=Put(out,size,pos,",\"lan\":[");
		for(int i=0,n=0;ok&&i<a->numlan&&i<LOBBY_MAXLAN;i++)
			if(LobbyLANAddress(a->lan[i]))
				ok=(!n++||Put(out,size,pos,","))&&PutString(out,size,pos,a->lan[i]);
		ok=ok&&Put(out,size,pos,"]");
	}
	snprintf(num,sizeof(num),",\"port\":%d,\"protocol\":%d,\"players\":%d,\"maxplayers\":%d,\"mode\":%d}",
		a->port,a->protocol,a->players,a->maxplayers,a->mode);
	return ok&&Put(out,size,pos,num);
}

//---- HTTP ----

static char lobbyurl[201];
static char lobbyagent[64];

const char *LobbyGetURL()
{
	return lobbyurl;
}

int LobbySetURL(const char *url,const char *useragent)
{
	lobbyurl[0]=0;
	snprintf(lobbyagent,sizeof(lobbyagent),"%s",useragent?useragent:"Edifice");
	if(!url) return 0;
	int n=strlen(url);
	while(n&&url[n-1]=='/') n--;   //"https://host/" and "https://host" are the same
	if(n>=(int)sizeof(lobbyurl)) return 0;
	if(strncmp(url,"http://",7)&&strncmp(url,"https://",8)) return 0;
	for(int i=0;i<n;i++)
		if(url[i]<=32||url[i]>126) return 0;
	memcpy(lobbyurl,url,n);
	lobbyurl[n]=0;
	return LobbyHttpAvailable();
}

#if defined(EDIFICE_LOBBY_CURL)

#include <curl/curl.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

struct LobbyRequest
{
	std::string method,url,body,token,agent;
	int ipv4;
	int done;
	int dropped;
	int status;
	std::string reply;
};

static std::mutex lobbylock;
static std::condition_variable wake;    //worker: a request is queued, or stop
static std::condition_variable idle;    //LobbyShutdown: no request left
static std::deque<LobbyRequest *> pending;
static std::map<int,LobbyRequest *> requests;   //every request not polled yet
static std::thread worker;
static std::atomic<int> stopping(0);
static int busy;
static int nextrequest=1;
static int started;

int LobbyHttpAvailable()
{
	return 1;
}

static size_t Received(char *data,size_t size,size_t count,void *user)
{
	std::string *reply=(std::string *)user;
	size_t n=size*count;
	if(reply->size()+n>LOBBY_MAXREPLY) return 0;   //aborts the transfer
	reply->append(data,n);
	return n;
}

static int Progress(void *,curl_off_t,curl_off_t,curl_off_t,curl_off_t)
{
	return stopping.load();   //nonzero aborts
}

static void Perform(LobbyRequest *r)
{
	CURL *curl=curl_easy_init();
	if(!curl)
	{
		r->status=0;
		r->reply="libcurl failed to start";
		return;
	}
	struct curl_slist *headers=0;
	headers=curl_slist_append(headers,"Accept: application/json");
	headers=curl_slist_append(headers,"Expect:");
	if(!r->body.empty()) headers=curl_slist_append(headers,"Content-Type: application/json");
	std::string auth;
	if(!r->token.empty())
	{
		auth="Authorization: Bearer "+r->token;
		headers=curl_slist_append(headers,auth.c_str());
	}
	char error[CURL_ERROR_SIZE]="";
	curl_easy_setopt(curl,CURLOPT_URL,r->url.c_str());
	curl_easy_setopt(curl,CURLOPT_CUSTOMREQUEST,r->method.c_str());
	if(r->method=="POST"||r->method=="PUT")
	{
		curl_easy_setopt(curl,CURLOPT_POSTFIELDS,r->body.c_str());
		curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,(long)r->body.size());
	}
	curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
	curl_easy_setopt(curl,CURLOPT_USERAGENT,r->agent.c_str());
	curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,Received);
	curl_easy_setopt(curl,CURLOPT_WRITEDATA,&r->reply);
	curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
	curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,Progress);
	curl_easy_setopt(curl,CURLOPT_ERRORBUFFER,error);
	curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);   //threads: no SIGALRM for DNS timeouts
	curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,5L);
	curl_easy_setopt(curl,CURLOPT_TIMEOUT,10L);
	curl_easy_setopt(curl,CURLOPT_MAXREDIRS,0L);
	if(r->ipv4) curl_easy_setopt(curl,CURLOPT_IPRESOLVE,(long)CURL_IPRESOLVE_V4);
	//tests only: a lobby with a self-signed certificate (tests/net_p2p.py); the
	//certificate is still checked, against that file instead of the system's
	const char *ca=getenv("EDIFICE_TEST_CA_FILE");
	if(ca&&ca[0]) curl_easy_setopt(curl,CURLOPT_CAINFO,ca);
	CURLcode result=curl_easy_perform(curl);
	long status=0;
	curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
	if(result!=CURLE_OK)
	{
		r->status=0;
		r->reply=error[0]?error:curl_easy_strerror(result);
	}
	else
		r->status=(int)status;
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
}

static void Work()
{
	std::unique_lock<std::mutex> l(lobbylock);
	while(1)
	{
		wake.wait(l,[]{ return stopping.load()||!pending.empty(); });
		if(stopping.load()) break;
		LobbyRequest *r=pending.front();
		pending.pop_front();
		busy=1;
		l.unlock();
		Perform(r);
		l.lock();
		busy=0;
		r->done=1;
		if(r->dropped)
		{
			for(std::map<int,LobbyRequest *>::iterator i=requests.begin();i!=requests.end();++i)
				if(i->second==r) { requests.erase(i); break; }
			delete r;
		}
		if(pending.empty()) idle.notify_all();
	}
}

int LobbySend(const char *method,const char *path,const char *body,const char *token,int ipv4)
{
	if(!lobbyurl[0]||!method||!path||path[0]!='/') return 0;
	std::lock_guard<std::mutex> l(lobbylock);
	if(stopping.load()) return 0;
	if(!started)
	{
		curl_global_init(CURL_GLOBAL_DEFAULT);
		worker=std::thread(Work);
		started=1;
	}
	if(pending.size()>=16) return 0;   //the lobby isn't answering: don't pile up
	LobbyRequest *r=new LobbyRequest;
	r->method=method;
	r->url=std::string(lobbyurl)+path;
	if(body) r->body=body;
	if(token) r->token=token;
	r->agent=lobbyagent;
	r->ipv4=ipv4;
	r->done=r->dropped=0;
	r->status=0;
	int id=nextrequest++;
	if(nextrequest<=0) nextrequest=1;
	requests[id]=r;
	pending.push_back(r);
	wake.notify_one();
	return id;
}

int LobbyPoll(int request,int *status,std::string *reply)
{
	std::lock_guard<std::mutex> l(lobbylock);
	std::map<int,LobbyRequest *>::iterator i=requests.find(request);
	if(i==requests.end())
	{
		*status=0;
		if(reply) *reply="no such request";
		return 1;
	}
	LobbyRequest *r=i->second;
	if(!r->done) return 0;
	*status=r->status;
	if(reply) reply->swap(r->reply);
	requests.erase(i);
	delete r;
	return 1;
}

void LobbyDrop(int request)
{
	std::lock_guard<std::mutex> l(lobbylock);
	std::map<int,LobbyRequest *>::iterator i=requests.find(request);
	if(i==requests.end()) return;
	if(i->second->done)
	{
		delete i->second;
		requests.erase(i);
	}
	else
		i->second->dropped=1;
}

void LobbyShutdown(int waitms)
{
	{
		std::unique_lock<std::mutex> l(lobbylock);
		if(!started) return;
		idle.wait_for(l,std::chrono::milliseconds(waitms),[]{ return pending.empty()&&!busy; });
		stopping=1;
		wake.notify_all();
	}
	worker.join();
	std::lock_guard<std::mutex> l(lobbylock);
	for(std::map<int,LobbyRequest *>::iterator i=requests.begin();i!=requests.end();++i)
		delete i->second;
	requests.clear();
	pending.clear();
	curl_global_cleanup();
	started=0;
	stopping=0;
}

#elif defined(__EMSCRIPTEN__)

//The web build (roadmap M14): the browser's fetch through emscripten_fetch, without
//threads. Requests run in the browser and their callbacks come between two frames,
//on the game's thread: the game sends then polls exactly as with the curl worker.
//The lobby must allow the page's origin (CORS, EDIFICE_CORS_ORIGINS in server/).
//The browser chooses the address family and the User-Agent (ipv4 and the agent
//are ignored: only a host needs the lobby to see its IPv4, and the web build
//doesn't host Internet games).

#include <emscripten/fetch.h>
#include <map>

struct LobbyRequest
{
	std::string method,url,body,auth;
	const char *headers[7];
	int done;
	int dropped;
	int status;
	std::string reply;
};

static std::map<int,LobbyRequest *> requests;   //every request not polled yet
static int nextrequest=1;

int LobbyHttpAvailable()
{
	return 1;
}

//success and failure alike: an HTTP status (or 0 when it couldn't be fetched)
static void Finished(emscripten_fetch_t *f)
{
	LobbyRequest *r=(LobbyRequest *)f->userData;
	r->status=f->status;
	if(f->numBytes>LOBBY_MAXREPLY)
	{
		r->status=0;
		r->reply="the lobby's answer is too long";
	}
	else if(f->status==0)
		//a network error, or the lobby doesn't allow this page (CORS): the browser
		//doesn't tell which
		r->reply=f->statusText[0]?f->statusText:"network error (or the lobby doesn't allow this page)";
	else if(f->data&&f->numBytes>0)
		r->reply.assign(f->data,(size_t)f->numBytes);
	r->done=1;
	emscripten_fetch_close(f);
	if(r->dropped) delete r;   //LobbyDrop forgot it already
}

int LobbySend(const char *method,const char *path,const char *body,const char *token,int ipv4)
{
	(void)ipv4;
	if(!lobbyurl[0]||!method||!path||path[0]!='/') return 0;
	if(strlen(method)>=sizeof(((emscripten_fetch_attr_t *)0)->requestMethod)) return 0;
	int running=0;
	for(std::map<int,LobbyRequest *>::iterator i=requests.begin();i!=requests.end();++i)
		if(!i->second->done) running++;
	if(running>=16) return 0;   //the lobby isn't answering: don't pile up
	LobbyRequest *r=new LobbyRequest;
	r->method=method;
	r->url=std::string(lobbyurl)+path;
	if(body) r->body=body;
	r->done=r->dropped=0;
	r->status=0;
	int h=0;
	r->headers[h++]="Accept";
	r->headers[h++]="application/json";
	if(body)
	{
		r->headers[h++]="Content-Type";
		r->headers[h++]="application/json";
	}
	if(token)
	{
		r->auth=std::string("Bearer ")+token;
		r->headers[h++]="Authorization";
		r->headers[h++]=r->auth.c_str();
	}
	r->headers[h]=0;
	int id=nextrequest++;
	if(nextrequest<=0) nextrequest=1;
	requests[id]=r;

	emscripten_fetch_attr_t attr;
	emscripten_fetch_attr_init(&attr);
	strcpy(attr.requestMethod,method);
	attr.attributes=EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
	attr.onsuccess=Finished;
	attr.onerror=Finished;
	attr.userData=r;
	attr.requestHeaders=r->headers;
	if(body)
	{
		attr.requestData=r->body.data();
		attr.requestDataSize=r->body.size();
	}
	attr.timeoutMSecs=10000;
	emscripten_fetch(&attr,r->url.c_str());
	return id;
}

int LobbyPoll(int request,int *status,std::string *reply)
{
	std::map<int,LobbyRequest *>::iterator i=requests.find(request);
	if(i==requests.end())
	{
		*status=0;
		if(reply) *reply="no such request";
		return 1;
	}
	LobbyRequest *r=i->second;
	if(!r->done) return 0;
	*status=r->status;
	if(reply) reply->swap(r->reply);
	requests.erase(i);
	delete r;
	return 1;
}

void LobbyDrop(int request)
{
	std::map<int,LobbyRequest *>::iterator i=requests.find(request);
	if(i==requests.end()) return;
	if(i->second->done) delete i->second;
	else i->second->dropped=1;   //Finished deletes it
	requests.erase(i);
}

//nothing to wait for: the page goes away with its requests, and a closing host
//(none on the web) would only have its DELETE in flight
void LobbyShutdown(int)
{
	for(std::map<int,LobbyRequest *>::iterator i=requests.begin();i!=requests.end();++i)
	{
		if(i->second->done) delete i->second;
		else i->second->dropped=1;
	}
	requests.clear();
}

#else   //built without libcurl: no Internet list

int LobbyHttpAvailable() { return 0; }
int LobbySend(const char *,const char *,const char *,const char *,int) { return 0; }
int LobbyPoll(int,int *status,std::string *reply)
{
	*status=0;
	if(reply) *reply="built without libcurl";
	return 1;
}
void LobbyDrop(int) {}
void LobbyShutdown(int) {}

#endif
