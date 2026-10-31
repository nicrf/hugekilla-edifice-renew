//version trailer of REQUESTENTER and INFO, and the compatibility rule (version.h).
//Apart from multiplay.cpp so that the parser can be tested and fuzzed without the game.

#include <stdio.h>
#include <string.h>
#include "wire.h"
#include "version.h"

int WriteVersionTrailer(char *buf)
{
	ByteWriter w(buf,VERSIONTRAILER_MAX);
	w.U32(EDIFICE_NET_MAGIC);
	w.U16(EDIFICE_PROTOCOL_VERSION);
	w.U32(EDIFICE_NET_LAYOUT);   //fixed since protocol 4 (explicit serialization)
	int n=strlen(EDIFICE_VERSION_STRING);
	if(n>15) n=15;
	w.U8(n);
	w.Bytes(EDIFICE_VERSION_STRING,n);
	return w.Size();
}

void ReadVersionTrailer(const char *buf,int len,PeerVersion *out)
{
	const unsigned char *p=(const unsigned char *)buf;
	memset(out,0,sizeof(*out));
	out->protocol=1;
	strcpy(out->version,"old");
	if(len<11) return;
	unsigned int magic=p[0]|(p[1]<<8)|(p[2]<<16)|((unsigned int)p[3]<<24);
	if(magic!=EDIFICE_NET_MAGIC) return;
	out->present=1;
	out->protocol=p[4]|(p[5]<<8);
	out->layout=p[6]|(p[7]<<8)|(p[8]<<16)|((unsigned int)p[9]<<24);
	int n=p[10];
	if(n>15) n=15;
	if(n>len-11) n=len-11;
	for(int i=0;i<n;i++)
		out->version[i]=(p[11+i]>=32&&p[11+i]<127)?p[11+i]:'?';
	out->version[n]=0;
}

int VersionCompatible(const PeerVersion *peer,char *reason,int reasonsize)
{
	//the protocol alone decides: messages no longer depend on the struct layout,
	//so peer->layout is informative (older protocols still differ by protocol)
	if(peer->present&&peer->protocol==EDIFICE_PROTOCOL_VERSION)
	{
		if(reasonsize) reason[0]=0;
		return 1;
	}
	if(!peer->present)
		snprintf(reason,reasonsize,"original game (p1), you %s p%d",EDIFICE_VERSION_STRING,EDIFICE_PROTOCOL_VERSION);
	else
		snprintf(reason,reasonsize,"other %s p%d, you %s p%d",peer->version,peer->protocol,EDIFICE_VERSION_STRING,EDIFICE_PROTOCOL_VERSION);
	return 0;
}
