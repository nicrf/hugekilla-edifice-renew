// Unit test of the signaling relay protocol (src/net/signaling.cpp, roadmap M7d):
// the lines the client writes, the readers of what the relay sends (hostile lines
// included), the from_identity a GameNetworkingSockets signal claims, and the
// STUN/TURN and relay addresses of a lobby session, and the same relay over
// WebSocket (the upgrade, frames both ways).
#include "signaling.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); failures++; } } while(0)

static const char *ME="str:edifice-0011223344556677";
static const char *HOST="str:edifice-8899aabbccddeeff";

static int Parse(const std::string &line,char *sender,unsigned char *payload,int *len)
{
	return SignalParseLine(line.data(),(int)line.size(),sender,SIGNAL_MAXIDENTITY+1,payload,SIGNAL_MAXPAYLOAD,len);
}

//a protobuf string field
static void Field(std::string &msg,int field,const std::string &value)
{
	msg.push_back((char)(field<<3|2));
	msg.push_back((char)value.size());
	msg+=value;
}

static int From(const std::string &msg,char *identity)
{
	return SignalFromIdentity((const unsigned char *)msg.data(),(int)msg.size(),identity,SIGNAL_MAXIDENTITY+1);
}

static void Lines()
{
	std::string line;
	CHECK(SignalHelloLine(ME,"00112233445566778899aabbccddeeff",&line)&&line==std::string(ME)+" 00112233445566778899aabbccddeeff\n");
	CHECK(!SignalHelloLine(ME,"bad token",&line));
	CHECK(!SignalHelloLine(ME,"tok\nX",&line));
	CHECK(!SignalHelloLine("edifice-0011","ab",&line));
	CHECK(!SignalHelloLine("str:a b","ab",&line));

	const unsigned char sig[3]={0x42,0x00,0xff};
	CHECK(SignalFormatLine(HOST,sig,3,&line)&&line==std::string(HOST)+" 4200ff\n");
	CHECK(!SignalFormatLine("str:x\ny",sig,3,&line));
	CHECK(!SignalFormatLine(HOST,sig,0,&line));
	static unsigned char big[SIGNAL_MAXPAYLOAD+1];
	CHECK(SignalFormatLine(HOST,big,SIGNAL_MAXPAYLOAD,&line)&&(int)line.size()<=SIGNAL_MAXLINE);
	CHECK(!SignalFormatLine(HOST,big,SIGNAL_MAXPAYLOAD+1,&line));

	char sender[SIGNAL_MAXIDENTITY+1];
	static unsigned char payload[SIGNAL_MAXPAYLOAD];
	int len;
	CHECK(Parse(std::string(HOST)+" 4200FF",sender,payload,&len)==1&&!strcmp(sender,HOST)&&len==3&&
		payload[0]==0x42&&payload[2]==0xff);
	CHECK(Parse(std::string(HOST)+" 4200ff\r",sender,payload,&len)==1&&len==3);
	CHECK(Parse("ERROR unauthorized",sender,payload,&len)==-1);
	CHECK(Parse("ERRORS x",sender,payload,&len)==0);
	CHECK(Parse(std::string(HOST)+" 420",sender,payload,&len)==0);     // odd length
	CHECK(Parse(std::string(HOST)+" 42zz",sender,payload,&len)==0);    // not hex
	CHECK(Parse(std::string(HOST)+" ",sender,payload,&len)==0);
	CHECK(Parse(std::string(HOST)+" 42 00",sender,payload,&len)==0);
	CHECK(Parse("str:edi\x01" "fice 4200",sender,payload,&len)==0);   // control character in the sender
	CHECK(Parse("steamid:76561 4200",sender,payload,&len)==0);         // not a session identity
	std::string longid="str:"+std::string(61,'a');
	CHECK(Parse(longid+" 4200",sender,payload,&len)==0);
	CHECK(Parse("noidentity",sender,payload,&len)==0&&!sender[0]&&len==0);
	std::string huge=std::string(HOST)+" "+std::string(SIGNAL_MAXLINE,'0');
	CHECK(Parse(huge,sender,payload,&len)==0);
	// a small payload buffer is never overrun
	unsigned char two[2];
	CHECK(SignalParseLine("str:ab 001122",13,sender,sizeof(sender),two,2,&len)==0);
	CHECK(SignalParseLine("str:ab 0011",11,sender,sizeof(sender),two,2,&len)==1&&len==2);
	char tiny[4];
	CHECK(SignalParseLine("str:ab 0011",11,tiny,sizeof(tiny),two,2,&len)==0);
}

static void Reader()
{
	SignalReader r;
	std::string line;
	r.Add("a b\nc",5);
	CHECK(r.Next(&line)&&line=="a b");
	CHECK(!r.Next(&line));
	r.Add("d\n",2);
	CHECK(r.Next(&line)&&line=="cd");
	// an overlong line is skipped whole, the next one still comes
	std::string flood(SIGNAL_MAXLINE*3,'x');
	r.Add("ok\n",3);
	r.Add(flood.data(),(int)flood.size());
	r.Add("\nafter\n",7);
	CHECK(r.Next(&line)&&line=="ok");
	CHECK(r.Next(&line)&&line=="after");
	CHECK(!r.Next(&line));
	// many short lines at once: none lost
	std::string many;
	for(int i=0;i<2000;i++) many+="l\n";
	r.Add(many.data(),(int)many.size());
	int n=0;
	while(r.Next(&line)) n+=line=="l";
	CHECK(n==2000);
}

static void FromIdentity()
{
	char id[SIGNAL_MAXIDENTITY+1];
	std::string msg;
	// to_connection_id (fixed32, field 1), from_identity, from_connection_id (fixed32, 9)
	msg+="\x0d\x01\x02\x03\x04";
	Field(msg,8,HOST);
	msg+="\x4d\x05\x06\x07\x08";
	CHECK(From(msg,id)&&!strcmp(id,HOST));
	// repeated: the last one counts, as for protobuf
	std::string twice=msg;
	Field(twice,8,ME);
	CHECK(From(twice,id)&&!strcmp(id,ME));
	// nested messages are skipped (connect_request, 4)
	std::string nested;
	Field(nested,4,std::string("\x42\x05str:x",7));
	Field(nested,8,HOST);
	CHECK(From(nested,id)&&!strcmp(id,HOST));
	std::string none="\x0d\x01\x02\x03\x04";
	CHECK(!From(none,id)&&!id[0]);
	std::string cut=msg.substr(0,msg.size()-2);
	CHECK(!From(cut,id));
	std::string badlen=msg;
	badlen[6]=(char)100;                       // longer than the message
	CHECK(!From(badlen,id));
	std::string notid;
	Field(notid,8,"steamid:123");
	CHECK(!From(notid,id));
	std::string wrongtype="\x40\x01";             // field 8 as a varint
	CHECK(!From(wrongtype,id));
	std::string group="\x0b\x0c";                 // groups: refused
	CHECK(!From(group,id));
	std::string longvarint="\x08\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x01";
	CHECK(!From(longvarint,id));
	std::string zerofield="\x02\x00";
	CHECK(!From(zerofield,id));
}

static void Addresses()
{
	char host[64],out[300];
	int port;
	CHECK(SignalParseAddress("lobby.example.org:10000",host,sizeof(host),&port)&&!strcmp(host,"lobby.example.org")&&port==10000);
	CHECK(SignalParseAddress("10.1.2.3:1",host,sizeof(host),&port)&&port==1);
	CHECK(!SignalParseAddress("lobby:0",host,sizeof(host),&port));
	CHECK(!SignalParseAddress("lobby:65536",host,sizeof(host),&port));
	CHECK(!SignalParseAddress("lobby",host,sizeof(host),&port));
	CHECK(!SignalParseAddress(":10000",host,sizeof(host),&port));
	CHECK(!SignalParseAddress("a b:10",host,sizeof(host),&port));
	CHECK(!SignalParseAddress("[::1]:10",host,sizeof(host),&port));
	CHECK(SignalIceServer("stun:lobby.example.org:3478","stun",out,sizeof(out))&&!strcmp(out,"lobby.example.org:3478"));
	CHECK(SignalIceServer("turn:10.1.2.3:3478?transport=udp","turn",out,sizeof(out))&&!strcmp(out,"10.1.2.3:3478"));
	CHECK(SignalIceServer("turn:x:3478","turn",out,sizeof(out))&&!strcmp(out,"x:3478"));
	CHECK(!SignalIceServer("turn:x:3478?transport=tcp","turn",out,sizeof(out)));
	CHECK(!SignalIceServer("turns:x:5349","turn",out,sizeof(out)));
	CHECK(!SignalIceServer("stun:x:3478","turn",out,sizeof(out)));
	CHECK(!SignalIceServer("stun:x,y:3478","stun",out,sizeof(out)));
	CHECK(!SignalIceServer("stun:x","stun",out,sizeof(out)));
	CHECK(SignalValidIdentity(ME)&&!SignalValidIdentity("str:")&&!SignalValidIdentity(0)&&!SignalValidIdentity("ip:1.2.3.4"));
}

static void WebSocket()
{
	char host[64],path[64];
	int port,tls;
	CHECK(SignalParseURL("wss://lobby.example.org/v1/signal",host,sizeof(host),&port,path,sizeof(path),&tls)&&
		!strcmp(host,"lobby.example.org")&&port==443&&tls==1&&!strcmp(path,"/v1/signal"));
	CHECK(SignalParseURL("ws://127.0.0.1:18080/v1/signal",host,sizeof(host),&port,path,sizeof(path),&tls)&&
		!strcmp(host,"127.0.0.1")&&port==18080&&tls==0&&!strcmp(path,"/v1/signal"));
	CHECK(SignalParseURL("wss://x",host,sizeof(host),&port,path,sizeof(path),&tls)&&port==443&&!strcmp(path,"/"));
	CHECK(SignalParseURL("wss://x:8443",host,sizeof(host),&port,path,sizeof(path),&tls)&&port==8443&&!strcmp(path,"/"));
	CHECK(!SignalParseURL("https://x/v1/signal",host,sizeof(host),&port,path,sizeof(path),&tls));
	CHECK(!SignalParseURL("wss://user@x/",host,sizeof(host),&port,path,sizeof(path),&tls));
	CHECK(!SignalParseURL("wss://x:0/",host,sizeof(host),&port,path,sizeof(path),&tls));
	CHECK(!SignalParseURL("wss://x:/",host,sizeof(host),&port,path,sizeof(path),&tls));
	CHECK(!SignalParseURL("wss:///v1",host,sizeof(host),&port,path,sizeof(path),&tls));
	CHECK(!SignalParseURL("wss://x/a b",host,sizeof(host),&port,path,sizeof(path),&tls));
	CHECK(!SignalParseURL("wss://[::1]/",host,sizeof(host),&port,path,sizeof(path),&tls));
	CHECK(!SignalParseURL(0,host,sizeof(host),&port,path,sizeof(path),&tls));

	// RFC 6455 1.3: the key "the sample nonce"
	const unsigned char key[16]={'t','h','e',' ','s','a','m','p','l','e',' ','n','o','n','c','e'};
	std::string req=SignalWsRequest("lobby.example.org",443,1,"/v1/signal",key);
	CHECK(req=="GET /v1/signal HTTP/1.1\r\nHost: lobby.example.org\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n");
	CHECK(SignalWsRequest("x",8443,1,"/",key).find("Host: x:8443\r\n")!=std::string::npos);

	const char *ok="HTTP/1.1 101 Switching Protocols\r\nupgrade:  WebSocket \r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n\x81\x02hi";
	int oklen=(int)strlen(ok);
	CHECK(SignalWsResponse(ok,oklen)==oklen-4);
	CHECK(SignalWsResponse(ok,40)==0);
	const char *refused="HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{}";
	CHECK(SignalWsResponse(refused,(int)strlen(refused))==-1);
	const char *noupgrade="HTTP/1.1 101 Switching Protocols\r\nUpgrade: h2c\r\n\r\n";
	CHECK(SignalWsResponse(noupgrade,(int)strlen(noupgrade))==-1);
	std::string endless="HTTP/1.1 101 OK\r\n"+std::string(SIGNAL_WS_MAXRESPONSE,'a');
	CHECK(SignalWsResponse(endless.data(),(int)endless.size())==-1);

	// client frames are masked
	const unsigned char mask[4]={0x37,0xfa,0x21,0x3d};
	std::string frame;
	SignalWsFrame(SIGNAL_WS_TEXT,"Hello",5,mask,&frame);
	CHECK(frame=="\x81\x85\x37\xfa\x21\x3d\x7f\x9f\x4d\x51\x58");   // RFC 6455 5.7
	frame.clear();
	std::string big(300,'a');
	SignalWsFrame(SIGNAL_WS_TEXT,big.data(),(int)big.size(),mask,&frame);
	CHECK(frame.size()==8+300&&(unsigned char)frame[1]==(0x80|126)&&frame[2]==1&&frame[3]==44);

	// server frames, in pieces
	SignalWsReader r;
	int op;
	std::string payload;
	std::string in=std::string("\x81\x05Hello",7)+std::string("\x89\x00",2)+std::string("\x82\x7e\x01\x2c",4)+big;
	for(size_t i=0;i<in.size();i+=3) r.Add(in.data()+i,(int)(in.size()-i<3?in.size()-i:3));
	CHECK(r.Next(&op,&payload)==1&&op==SIGNAL_WS_TEXT&&payload=="Hello");
	CHECK(r.Next(&op,&payload)==1&&op==SIGNAL_WS_PING&&payload.empty());
	CHECK(r.Next(&op,&payload)==1&&op==SIGNAL_WS_BINARY&&payload==big);
	CHECK(r.Next(&op,&payload)==0);
	// an overlong frame is skipped whole, what follows is read
	std::string huge(SIGNAL_MAXLINE+10,'z');
	in=std::string("\x81\x7e",2)+(char)(huge.size()>>8)+(char)(huge.size()&255)+huge+std::string("\x81\x02ok",4);
	for(size_t i=0;i<in.size();i+=1000) r.Add(in.data()+i,(int)(in.size()-i<1000?in.size()-i:1000));
	CHECK(r.Next(&op,&payload)==1&&payload=="ok");
	std::string bigger=std::string("\x81\x7f\x00\x00\x00\x00\x00\x10\x00\x00",10);   // 1 MB announced
	r.Add(bigger.data(),(int)bigger.size());
	CHECK(r.Next(&op,&payload)==0);
	// protocol errors: masked, fragmented, continuation, reserved bits, long control frame
	const char *bad[]={"\x81\x82" "abcdxx","\x01\x01x","\x80\x01x","\xc1\x01x","\x89\x7e\x00\x80"};
	const int badlen[]={8,3,3,3,4};
	for(int i=0;i<5;i++)
	{
		SignalWsReader e;
		e.Add(bad[i],badlen[i]);
		CHECK(e.Next(&op,&payload)==-1);
	}
}

// WebRTC signals (M14 task 3): offer, answer, candidates through the same relay
static void Rtc()
{
	SignalRtc s,back;
	char sender[SIGNAL_MAXIDENTITY+1];
	std::string line;
	const char *sdp="v=0\r\no=- 46117 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\na=group:BUNDLE 0\r\n"
		"m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\na=sctp-port:5000\r\n";
	s.kind='o';
	strcpy(s.cid,"k3J9");
	s.a=sdp;
	CHECK(SignalRtcFormat(HOST,&s,&line));
	CHECK(line.compare(0,strlen(HOST)+13,std::string(HOST)+" rtc1:o:k3J9:")==0&&line[line.size()-1]=='\n');
	CHECK(line.find(' ',strlen(HOST)+1)==std::string::npos&&line.find('=')==std::string::npos);
	// the relay hands it on as "SENDER PAYLOAD": the same text after the space
	std::string relayed=std::string(ME)+line.substr(strlen(HOST),line.size()-strlen(HOST)-1);
	CHECK(SignalRtcParseLine(relayed.data(),(int)relayed.size(),sender,sizeof(sender),&back)==1);
	CHECK(!strcmp(sender,ME)&&back.kind=='o'&&!strcmp(back.cid,"k3J9")&&back.a==sdp&&back.b.empty());
	// ... and it isn't a GameNetworkingSockets line, nor the reverse
	unsigned char payload[SIGNAL_MAXPAYLOAD];
	int plen;
	CHECK(SignalParseLine(relayed.data(),(int)relayed.size(),sender,sizeof(sender),payload,sizeof(payload),&plen)==0);
	std::string hex;
	CHECK(SignalFormatLine(ME,"\x0a\x02hi",4,&hex));
	CHECK(SignalRtcParseLine(hex.data(),(int)hex.size()-1,sender,sizeof(sender),&back)==0&&!sender[0]);
	// every length of text survives (base64url without padding)
	for(int n=1;n<40;n++)
	{
		s.kind='c';
		s.a=std::string(n,'a'+n%26)+" udp 2122260223 192.168.1.2 54321 typ host";
		s.b=std::string(n%3,'0');
		CHECK(SignalRtcFormat(HOST,&s,&line));
		CHECK(SignalRtcParse(line.data()+strlen(HOST)+1,(int)line.size()-(int)strlen(HOST)-2,&back)==1);
		CHECK(back.kind=='c'&&back.a==s.a&&back.b==s.b);
	}
	// refused: bad kind, cid, texts, sizes, a mid on an answer
	s.kind='x';
	CHECK(!SignalRtcFormat(HOST,&s,&line));
	s.kind='a';
	s.b="0";
	CHECK(!SignalRtcFormat(HOST,&s,&line));
	s.b.clear();
	strcpy(s.cid,"a-b");
	CHECK(!SignalRtcFormat(HOST,&s,&line));
	strcpy(s.cid,"");
	CHECK(!SignalRtcFormat(HOST,&s,&line));
	strcpy(s.cid,"c1");
	s.a="bad\x01";
	CHECK(!SignalRtcFormat(HOST,&s,&line));
	s.a=std::string(SIGNAL_RTC_MAXTEXT+1,'v');
	CHECK(!SignalRtcFormat(HOST,&s,&line));
	s.a=std::string(SIGNAL_RTC_MAXTEXT,'v');   // the largest text still fits a relay line
	CHECK(SignalRtcFormat(HOST,&s,&line)&&(int)line.size()<=SIGNAL_MAXLINE);
	CHECK(!SignalRtcFormat("nobody",&s,&line));
	const char *bad[]={"rtc1:o:c1:","rtc1:o::YQ","rtc1:o:c1:Y","rtc1:o:c1:YQ:YQ","rtc1:c:c1:YQ:YQ:YQ","rtc1:q:c1:YQ",
		"rtc1:o:c1:YQ==","rtc1:o:c1:Y+Q","rtc1:oo:c1:YQ","rtc2:o:c1:YQ","rtc1:o:01234567890123456:YQ","rtc1:o:c1:AQ",
		"rtc1:o","rtc1:c:c1::YQ","rtc1:o:c-1:YQ"};
	for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++)
	{
		CHECK(SignalRtcParse(bad[i],(int)strlen(bad[i]),&back)==0);
		CHECK(back.kind==0&&!back.cid[0]&&back.a.empty()&&back.b.empty());
	}
	CHECK(SignalRtcParse("rtc1:o:c1:YQ",12,&back)==1&&back.a=="a");
	CHECK(SignalRtcParse("rtc1:c:c1:YQ:",13,&back)==1&&back.a=="a"&&back.b.empty());
	std::string spaced=std::string(ME)+" rtc1:o:c1:YQ\r";
	CHECK(SignalRtcParseLine(spaced.data(),(int)spaced.size(),sender,sizeof(sender),&back)==1);
	std::string noid="bob rtc1:o:c1:YQ";
	CHECK(SignalRtcParseLine(noid.data(),(int)noid.size(),sender,sizeof(sender),&back)==0);
}

int main()
{
	Rtc();
	Lines();
	Reader();
	FromIdentity();
	Addresses();
	WebSocket();
	if(failures) printf("%d failure(s)\n",failures);
	else printf("signaling: all tests passed\n");
	return failures?1:0;
}
