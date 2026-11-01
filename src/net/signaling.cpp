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
