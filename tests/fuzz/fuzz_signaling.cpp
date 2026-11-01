// What the signaling relay sends (src/net/signaling.cpp, roadmap M7d): a hostile
// or broken relay, or a peer, sends any bytes. The line reader stays bounded, the
// line reader and the protobuf check stay in bounds, and what they accept is valid:
// a session identity, a payload of the announced size, lines that fit.
// The first byte splits the input in chunks of that size, as recv() would.
#include "signaling.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_CHECK(c) do { if(!(c)) { fprintf(stderr,"%s:%d: fuzz check failed: %s\n",__FILE__,__LINE__,#c); abort(); } } while(0)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size);

static void Identity(const char *s)
{
	FUZZ_CHECK(SignalValidIdentity(s));
	FUZZ_CHECK(strlen(s)<=SIGNAL_MAXIDENTITY&&!strchr(s,' ')&&!strncmp(s,"str:",4));
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
	if(size<1||size>256*1024) return 0;
	int chunk=data[0]?data[0]:1;
	data++;
	size--;

	SignalReader reader;
	std::string line;
	static char sender[SIGNAL_MAXIDENTITY+1],claimed[SIGNAL_MAXIDENTITY+1];
	static unsigned char payload[SIGNAL_MAXPAYLOAD];
	for(size_t at=0;at<size;at+=chunk)
	{
		int n=(int)(size-at<(size_t)chunk?size-at:chunk);
		// a copy of exactly n bytes: ASan sees any read past it
		char *piece=(char *)malloc(n);
		memcpy(piece,data+at,n);
		reader.Add(piece,n);
		free(piece);
		while(reader.Next(&line))
		{
			FUZZ_CHECK((int)line.size()<SIGNAL_MAXLINE&&line.find('\n')==std::string::npos);
			char *copy=(char *)malloc(line.size()+1);
			memcpy(copy,line.data(),line.size());
			int len=-1;
			int r=SignalParseLine(copy,(int)line.size(),sender,sizeof(sender),payload,sizeof(payload),&len);
			free(copy);
			FUZZ_CHECK(r>=-1&&r<=1);
			if(r!=1) { FUZZ_CHECK(len==0); continue; }
			Identity(sender);
			FUZZ_CHECK(len>=1&&len<=SIGNAL_MAXPAYLOAD);
			unsigned char *msg=(unsigned char *)malloc(len);
			memcpy(msg,payload,len);
			if(SignalFromIdentity(msg,len,claimed,sizeof(claimed))) Identity(claimed);
			else FUZZ_CHECK(!claimed[0]);
			free(msg);
			// what we would send back fits a line
			std::string out;
			if(SignalFormatLine(sender,payload,len,&out)) FUZZ_CHECK((int)out.size()<=SIGNAL_MAXLINE);
		}
	}

	// the whole input as a signal, and as a session's addresses
	if(size>0)
	{
		unsigned char *msg=(unsigned char *)malloc(size);
		memcpy(msg,data,size);
		if(SignalFromIdentity(msg,(int)size,claimed,sizeof(claimed))) Identity(claimed);
		free(msg);
	}
	char text[301],host[64],out[300];
	int n=(int)(size<300?size:300),port;
	memcpy(text,data,n);
	text[n]=0;
	if(SignalParseAddress(text,host,sizeof(host),&port)) FUZZ_CHECK(host[0]&&port>=1&&port<=65535&&!strchr(host,':'));
	if(SignalIceServer(text,"turn",out,sizeof(out))) FUZZ_CHECK(!strchr(out,',')&&!strchr(out,' ')&&!strchr(out,'?'));
	return 0;
}
