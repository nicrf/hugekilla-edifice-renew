//signaling relay lines and the bits of a GameNetworkingSockets signal the client
//checks (see signaling.h)

#include "signaling.h"
#include <stdio.h>
#include <string.h>

static int IdentityChar(int c)
{
	return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='.'||c=='_'||c=='-';
}

//identity of n characters at s
static int ValidIdentity(const char *s,int n)
{
	if(n<5||n>SIGNAL_MAXIDENTITY||memcmp(s,"str:",4)) return 0;
	for(int i=4;i<n;i++)
		if(!IdentityChar((unsigned char)s[i])) return 0;
	return 1;
}

int SignalValidIdentity(const char *identity)
{
	return identity&&ValidIdentity(identity,(int)strnlen(identity,SIGNAL_MAXIDENTITY+1));
}

int SignalHelloLine(const char *identity,const char *token,std::string *line)
{
	if(!SignalValidIdentity(identity)||!token) return 0;
	int n=(int)strnlen(token,SIGNAL_MAXTOKEN+1);
	if(n<1||n>SIGNAL_MAXTOKEN) return 0;
	for(int i=0;i<n;i++)
		if(!((token[i]>='0'&&token[i]<='9')||(token[i]>='a'&&token[i]<='z')||(token[i]>='A'&&token[i]<='Z')))
			return 0;
	*line=identity;
	*line+=' ';
	*line+=token;
	*line+='\n';
	return 1;
}

int SignalFormatLine(const char *destination,const void *payload,int size,std::string *line)
{
	static const char hex[]="0123456789abcdef";
	if(!SignalValidIdentity(destination)||size<1||size>SIGNAL_MAXPAYLOAD) return 0;
	line->assign(destination);
	line->push_back(' ');
	const unsigned char *p=(const unsigned char *)payload;
	for(int i=0;i<size;i++)
	{
		line->push_back(hex[p[i]>>4]);
		line->push_back(hex[p[i]&15]);
	}
	line->push_back('\n');
	return (int)line->size()<=SIGNAL_MAXLINE;
}

static int HexDigit(int c)
{
	if(c>='0'&&c<='9') return c-'0';
	if(c>='a'&&c<='f') return c-'a'+10;
	if(c>='A'&&c<='F') return c-'A'+10;
	return -1;
}

int SignalParseLine(const char *line,int len,char *sender,int sendersize,
	unsigned char *payload,int maxpayload,int *payloadlen)
{
	*payloadlen=0;
	if(sendersize>0) sender[0]=0;
	if(!line||len<0||len>SIGNAL_MAXLINE) return 0;
	if(len&&line[len-1]=='\r') len--;
	if(len>=5&&!memcmp(line,"ERROR",5)&&(len==5||line[5]==' ')) return -1;
	const char *space=(const char *)memchr(line,' ',len);
	if(!space) return 0;
	int idlen=(int)(space-line);
	if(!ValidIdentity(line,idlen)||idlen>=sendersize) return 0;
	const char *hex=space+1;
	int hexlen=len-idlen-1;
	if(hexlen<2||hexlen%2||hexlen/2>maxpayload) return 0;
	for(int i=0;i<hexlen;i+=2)
	{
		int hi=HexDigit((unsigned char)hex[i]),lo=HexDigit((unsigned char)hex[i+1]);
		if(hi<0||lo<0) return 0;
		payload[i/2]=(unsigned char)(hi<<4|lo);
	}
	memcpy(sender,line,idlen);
	sender[idlen]=0;
	*payloadlen=hexlen/2;
	return 1;
}

//protobuf varint at *p (at most 10 bytes); 0 if cut or too long
static int Varint(const unsigned char *&p,const unsigned char *end,unsigned long long *value)
{
	*value=0;
	for(int shift=0;shift<70;shift+=7)
	{
		if(p>=end) return 0;
		unsigned char b=*p++;
		if(shift<64) *value|=(unsigned long long)(b&127)<<shift;
		if(!(b&128)) return 1;
	}
	return 0;
}

static int FromIdentity(const unsigned char *msg,int len,char *identity,int size);

int SignalFromIdentity(const unsigned char *msg,int len,char *identity,int size)
{
	if(size<1) return 0;
	int found=FromIdentity(msg,len,identity,size);
	if(!found) identity[0]=0;
	return found;
}

static int FromIdentity(const unsigned char *msg,int len,char *identity,int size)
{
	if(!msg||len<1) return 0;
	const unsigned char *p=msg,*end=msg+len;
	int found=0;
	while(p<end)
	{
		unsigned long long key,value;
		if(!Varint(p,end,&key)) return 0;
		unsigned long long field=key>>3;
		int type=(int)(key&7);
		if(field<1||field>536870911ULL) return 0;
		switch(type)
		{
		case 0:
			if(!Varint(p,end,&value)) return 0;
			break;
		case 1:
			if(end-p<8) return 0;
			p+=8;
			break;
		case 5:
			if(end-p<4) return 0;
			p+=4;
			break;
		case 2:
			if(!Varint(p,end,&value)||value>(unsigned long long)(end-p)) return 0;
			if(field==8)
			{
				//the last from_identity wins, as when protobuf parses it
				if(!ValidIdentity((const char *)p,(int)value)||(int)value>=size) return 0;
				memcpy(identity,p,(size_t)value);
				identity[value]=0;
				found=1;
			}
			p+=value;
			break;
		default:   //groups (3, 4) and the reserved types: not in these messages
			return 0;
		}
		if(field==8&&type!=2) return 0;
	}
	return found;
}

void SignalReader::Add(const char *data,int size)
{
	for(int i=0;i<size;i++)
	{
		char c=data[i];
		if(skipping)
		{
			if(c=='\n') skipping=0;
			continue;
		}
		buf.push_back(c);
		if(c=='\n') linelen=0;
		else if(++linelen>=SIGNAL_MAXLINE)
		{
			//too long to be one of ours: dropped up to its end
			buf.erase(buf.size()-linelen);
			linelen=0;
			skipping=1;
		}
	}
}

int SignalReader::Next(std::string *line)
{
	size_t n=buf.find('\n');
	if(n==std::string::npos) return 0;
	line->assign(buf,0,n);
	buf.erase(0,n+1);
	return 1;
}

//host:port at s (n characters): host letters, digits, '.', '-'
static int HostPort(const char *s,int n,char *host,int hostsize,int *port)
{
	const char *colon=0;
	for(int i=0;i<n;i++) if(s[i]==':') colon=s+i;
	if(!colon) return 0;
	int hl=(int)(colon-s);
	if(hl<1||hl>=hostsize||hl>253) return 0;
	for(int i=0;i<hl;i++)
	{
		int c=(unsigned char)s[i];
		if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='.'||c=='-')) return 0;
	}
	int digits=n-hl-1,v=0;
	if(digits<1||digits>5) return 0;
	for(int i=0;i<digits;i++)
	{
		int c=colon[1+i];
		if(c<'0'||c>'9') return 0;
		v=v*10+c-'0';
	}
	if(v<1||v>65535) return 0;
	memcpy(host,s,hl);
	host[hl]=0;
	*port=v;
	return 1;
}

int SignalParseAddress(const char *text,char *host,int hostsize,int *port)
{
	if(!text) return 0;
	int n=(int)strnlen(text,300);
	return n<300&&HostPort(text,n,host,hostsize,port);
}

int SignalIceServer(const char *url,const char *scheme,char *out,int size)
{
	if(!url||!scheme) return 0;
	int n=(int)strnlen(url,300),sl=(int)strlen(scheme);
	if(n>=300||n<=sl+1||strncmp(url,scheme,sl)||url[sl]!=':') return 0;
	const char *s=url+sl+1;
	n-=sl+1;
	const char *query=(const char *)memchr(s,'?',n);
	if(query)
	{
		//only UDP: the native ICE client has no TURN over TCP
		if(strcmp(query,"?transport=udp")) return 0;
		n=(int)(query-s);
	}
	char host[254];
	int port;
	if(!HostPort(s,n,host,sizeof(host),&port)) return 0;
	return snprintf(out,size,"%s:%d",host,port)<size;
}

//---- WebSocket ----

static int HostChar(int c)
{
	return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='.'||c=='-';
}

int SignalParseURL(const char *url,char *host,int hostsize,int *port,char *path,int pathsize,int *tls)
{
	if(!url) return 0;
	int n=(int)strnlen(url,300);
	if(n>=300) return 0;
	const char *s;
	int defport;
	if(!strncmp(url,"wss://",6)) { s=url+6; *tls=1; defport=443; }
	else if(!strncmp(url,"ws://",5)) { s=url+5; *tls=0; defport=80; }
	else return 0;
	const char *end=url+n;
	const char *slash=(const char *)memchr(s,'/',end-s);
	const char *hostend=slash?slash:end;
	const char *p=s;
	while(p<hostend&&*p!=':') p++;
	int hl=(int)(p-s);
	if(hl<1||hl>=hostsize||hl>253) return 0;
	for(int i=0;i<hl;i++)
		if(!HostChar((unsigned char)s[i])) return 0;
	int v=defport;
	if(p<hostend)
	{
		int digits=(int)(hostend-p-1);
		if(digits<1||digits>5) return 0;
		v=0;
		for(int i=0;i<digits;i++)
		{
			int c=(unsigned char)p[1+i];
			if(c<'0'||c>'9') return 0;
			v=v*10+c-'0';
		}
		if(v<1||v>65535) return 0;
	}
	const char *pth=slash?slash:"/";
	int pl=(int)(slash?end-slash:1);
	if(pl>=pathsize) return 0;
	for(int i=0;i<pl;i++)
		if((unsigned char)pth[i]<33||(unsigned char)pth[i]>126) return 0;
	memcpy(host,s,hl);
	host[hl]=0;
	memcpy(path,pth,pl);
	path[pl]=0;
	*port=v;
	return 1;
}

static void Base64(const unsigned char *in,int n,std::string *out)
{
	static const char digits[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	for(int i=0;i<n;i+=3)
	{
		unsigned v=(unsigned)in[i]<<16|(i+1<n?(unsigned)in[i+1]<<8:0)|(i+2<n?in[i+2]:0);
		*out+=digits[v>>18&63];
		*out+=digits[v>>12&63];
		*out+=i+1<n?digits[v>>6&63]:'=';
		*out+=i+2<n?digits[v&63]:'=';
	}
}

std::string SignalWsRequest(const char *host,int port,int tls,const char *path,const unsigned char key[16])
{
	char where[300];
	if(port==(tls?443:80)) snprintf(where,sizeof(where),"%s",host);
	else snprintf(where,sizeof(where),"%s:%d",host,port);
	std::string r="GET ";
	r+=path;
	r+=" HTTP/1.1\r\nHost: ";
	r+=where;
	r+="\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ";
	Base64(key,16,&r);
	r+="\r\n\r\n";
	return r;
}

static int Lower(int c)
{
	return c>='A'&&c<='Z'?c-'A'+'a':c;
}

//the n characters at s are text, ignoring case
static int Is(const char *s,int n,const char *text)
{
	if((int)strlen(text)!=n) return 0;
	for(int i=0;i<n;i++)
		if(Lower((unsigned char)s[i])!=Lower((unsigned char)text[i])) return 0;
	return 1;
}

//Sec-WebSocket-Accept isn't checked: it only shows that the server speaks
//WebSocket (against caches and confused servers); who the server is, the TLS of
//wss:// says.
int SignalWsResponse(const char *data,int len)
{
	if(!data||len<0) return -1;
	int end=-1;
	for(int i=0;i+4<=len&&i+4<=SIGNAL_WS_MAXRESPONSE;i++)
		if(!memcmp(data+i,"\r\n\r\n",4)) { end=i+4; break; }
	if(end<0) return len>=SIGNAL_WS_MAXRESPONSE?-1:0;
	if(end<16||memcmp(data,"HTTP/1.1 101 ",13)) return -1;
	int upgrade=0;
	const char *p=(const char *)memchr(data,'\n',end)+1;   //end: there is one
	while(p<data+end-2)
	{
		const char *eol=(const char *)memchr(p,'\r',data+end-p);
		if(!eol) return -1;
		const char *colon=(const char *)memchr(p,':',eol-p);
		if(colon)
		{
			const char *v=colon+1,*ve=eol;
			while(v<ve&&(*v==' '||*v=='\t')) v++;
			while(ve>v&&(ve[-1]==' '||ve[-1]=='\t')) ve--;
			if(Is(p,(int)(colon-p),"upgrade")&&Is(v,(int)(ve-v),"websocket")) upgrade=1;
		}
		p=eol+2;
	}
	return upgrade?end:-1;
}

void SignalWsFrame(int opcode,const char *payload,int size,const unsigned char mask[4],std::string *out)
{
	if(size<0) size=0;
	out->push_back((char)(0x80|(opcode&15)));
	if(size<126) out->push_back((char)(0x80|size));
	else if(size<=0xFFFF)
	{
		out->push_back((char)(0x80|126));
		out->push_back((char)(size>>8));
		out->push_back((char)size);
	}
	else
	{
		out->push_back((char)(0x80|127));
		for(int i=7;i>=0;i--) out->push_back((char)(i>=4?0:(unsigned)size>>(8*i)));
	}
	out->append((const char *)mask,4);
	for(int i=0;i<size;i++) out->push_back((char)(payload[i]^mask[i&3]));
}

void SignalWsReader::Add(const char *data,int size)
{
	if(skipping&&size>0)
	{
		int drop=skipping<(unsigned long long)size?(int)skipping:size;
		skipping-=drop;
		data+=drop;
		size-=drop;
	}
	if(size>0) buf.append(data,size);
}

int SignalWsReader::Next(int *opcode,std::string *payload)
{
	while(1)
	{
		if(skipping)
		{
			size_t drop=skipping<buf.size()?(size_t)skipping:buf.size();
			buf.erase(0,drop);
			skipping-=drop;
			if(skipping) return 0;
		}
		if(buf.size()<2) return 0;
		const unsigned char *b=(const unsigned char *)buf.data();
		int op=b[0]&15;
		if(b[0]&0x70||b[1]&0x80) return -1;   //extensions, or masked: not from a server
		if(op!=SIGNAL_WS_TEXT&&op!=SIGNAL_WS_BINARY&&op!=SIGNAL_WS_CLOSE&&op!=SIGNAL_WS_PING&&op!=SIGNAL_WS_PONG)
			return -1;   //continuations (nobody fragments here) and reserved opcodes
		if(!(b[0]&0x80)) return -1;
		unsigned long long n=b[1]&127;
		size_t head=2;
		if(n==126)
		{
			if(buf.size()<4) return 0;
			n=(unsigned long long)b[2]<<8|b[3];
			head=4;
		}
		else if(n==127)
		{
			if(buf.size()<10) return 0;
			n=0;
			for(int i=0;i<8;i++) n=n<<8|b[2+i];
			if(n>>63) return -1;
			head=10;
		}
		if(op>=SIGNAL_WS_CLOSE&&n>125) return -1;
		if(n>=SIGNAL_MAXLINE)
		{
			//too long to be one of ours: dropped whole
			buf.erase(0,head);
			skipping=n;
			continue;
		}
		if(buf.size()<head+n) return 0;
		*opcode=op;
		payload->assign(buf,head,(size_t)n);
		buf.erase(0,head+(size_t)n);
		return 1;
	}
}

//---- WebRTC signals ----

static const char b64url[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static void Base64Url(const std::string &in,std::string *out)
{
	const unsigned char *p=(const unsigned char *)in.data();
	int n=(int)in.size();
	for(int i=0;i<n;i+=3)
	{
		unsigned v=(unsigned)p[i]<<16|(i+1<n?(unsigned)p[i+1]<<8:0)|(i+2<n?p[i+2]:0);
		*out+=b64url[v>>18&63];
		*out+=b64url[v>>12&63];
		if(i+1<n) *out+=b64url[v>>6&63];
		if(i+2<n) *out+=b64url[v&63];
	}
}

static int Base64UrlDigit(int c)
{
	if(c>='A'&&c<='Z') return c-'A';
	if(c>='a'&&c<='z') return c-'a'+26;
	if(c>='0'&&c<='9') return c-'0'+52;
	if(c=='-') return 62;
	if(c=='_') return 63;
	return -1;
}

//n characters at s, unpadded: 1 and the bytes in out, 0 if not base64url or longer
//than max once decoded
static int UnBase64Url(const char *s,int n,int max,std::string *out)
{
	out->clear();
	if(n<0||n%4==1||(n/4)*3+(n%4?n%4-1:0)>max) return 0;
	unsigned v=0;
	int bits=0;
	for(int i=0;i<n;i++)
	{
		int d=Base64UrlDigit((unsigned char)s[i]);
		if(d<0) return 0;
		v=(v<<6|d)&0xffffff;
		bits+=6;
		if(bits>=8)
		{
			bits-=8;
			out->push_back((char)(v>>bits&255));
		}
	}
	return 1;
}

//what an SDP or a candidate may hold: printable ASCII, line ends, tabs
static int RtcText(const std::string &t,int needed)
{
	if((int)t.size()>SIGNAL_RTC_MAXTEXT||(needed&&t.empty())) return 0;
	for(size_t i=0;i<t.size();i++)
	{
		unsigned char c=(unsigned char)t[i];
		if((c<32||c>126)&&c!='\r'&&c!='\n'&&c!='\t') return 0;
	}
	return 1;
}

static int RtcValid(const SignalRtc *s)
{
	int n=(int)strnlen(s->cid,SIGNAL_RTC_MAXCID+1);
	if(n<1||n>SIGNAL_RTC_MAXCID) return 0;
	for(int i=0;i<n;i++)
	{
		char c=s->cid[i];
		if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z'))) return 0;
	}
	if(s->kind!='o'&&s->kind!='a'&&s->kind!='c') return 0;
	if(s->kind!='c'&&!s->b.empty()) return 0;
	return RtcText(s->a,1)&&RtcText(s->b,0);
}

static void RtcClear(SignalRtc *s)
{
	s->kind=0;
	s->cid[0]=0;
	s->a.clear();
	s->b.clear();
}

int SignalRtcFormat(const char *destination,const SignalRtc *s,std::string *line)
{
	if(!SignalValidIdentity(destination)||!RtcValid(s)) return 0;
	line->assign(destination);
	*line+=" " SIGNAL_RTC_PREFIX;
	line->push_back(s->kind);
	*line+=':';
	*line+=s->cid;
	*line+=':';
	Base64Url(s->a,line);
	if(s->kind=='c')
	{
		*line+=':';
		Base64Url(s->b,line);
	}
	line->push_back('\n');
	return (int)line->size()<=SIGNAL_MAXLINE;
}

int SignalRtcParse(const char *payload,int len,SignalRtc *s)
{
	RtcClear(s);
	const int pre=(int)sizeof(SIGNAL_RTC_PREFIX)-1;
	if(!payload||len<pre+4||len>SIGNAL_MAXLINE||memcmp(payload,SIGNAL_RTC_PREFIX,pre)) return 0;
	const char *p=payload+pre,*end=payload+len;
	if(p[1]!=':') return 0;
	char kind=p[0];
	p+=2;
	const char *colon=(const char *)memchr(p,':',end-p);
	if(!colon||colon-p<1||colon-p>SIGNAL_RTC_MAXCID) return 0;
	memcpy(s->cid,p,colon-p);
	s->cid[colon-p]=0;
	p=colon+1;
	const char *second=(const char *)memchr(p,':',end-p);
	if((second&&kind!='c')||
		!UnBase64Url(p,(int)((second?second:end)-p),SIGNAL_RTC_MAXTEXT,&s->a)||
		(second&&(memchr(second+1,':',end-second-1)||
			!UnBase64Url(second+1,(int)(end-second-1),SIGNAL_RTC_MAXTEXT,&s->b))))
	{
		RtcClear(s);
		return 0;
	}
	s->kind=kind;
	if(RtcValid(s)) return 1;
	RtcClear(s);
	return 0;
}

int SignalRtcParseLine(const char *line,int len,char *sender,int sendersize,SignalRtc *s)
{
	if(sendersize>0) sender[0]=0;
	RtcClear(s);
	if(!line||len<0||len>SIGNAL_MAXLINE) return 0;
	if(len&&line[len-1]=='\r') len--;
	const char *space=(const char *)memchr(line,' ',len);
	if(!space) return 0;
	int idlen=(int)(space-line);
	if(!ValidIdentity(line,idlen)||idlen>=sendersize) return 0;
	if(!SignalRtcParse(space+1,len-idlen-1,s)) return 0;
	memcpy(sender,line,idlen);
	sender[idlen]=0;
	return 1;
}
