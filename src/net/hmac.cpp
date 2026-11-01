//SHA-256, HMAC-SHA256 and the join cookie (hmac.h). Written for this project from
//FIPS 180-4 and RFC 2104; no third-party code.

#include <string.h>
#include <time.h>
#include <chrono>
#include <random>
#include "hmac.h"

static const unsigned int K[64]={
	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

static inline unsigned int Ror(unsigned int x,int n)
{
	return (x>>n)|(x<<(32-n));
}

static void Compress(unsigned int h[8],const unsigned char *p)
{
	unsigned int w[64];
	for(int i=0;i<16;i++)
		w[i]=((unsigned int)p[4*i]<<24)|((unsigned int)p[4*i+1]<<16)|((unsigned int)p[4*i+2]<<8)|p[4*i+3];
	for(int i=16;i<64;i++)
	{
		unsigned int s0=Ror(w[i-15],7)^Ror(w[i-15],18)^(w[i-15]>>3);
		unsigned int s1=Ror(w[i-2],17)^Ror(w[i-2],19)^(w[i-2]>>10);
		w[i]=w[i-16]+s0+w[i-7]+s1;
	}
	unsigned int a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],k=h[7];
	for(int i=0;i<64;i++)
	{
		unsigned int t1=k+(Ror(e,6)^Ror(e,11)^Ror(e,25))+((e&f)^(~e&g))+K[i]+w[i];
		unsigned int t2=(Ror(a,2)^Ror(a,13)^Ror(a,22))+((a&b)^(a&c)^(b&c));
		k=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
	}
	h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=k;
}

void Sha256Init(Sha256 *s)
{
	static const unsigned int iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
		0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
	memcpy(s->h,iv,sizeof(iv));
	s->used=0;
	s->bytes=0;
}

void Sha256Update(Sha256 *s,const void *data,size_t len)
{
	const unsigned char *p=(const unsigned char *)data;
	s->bytes+=len;
	while(len)
	{
		size_t n=SHA256_BLOCK-s->used;
		if(n>len) n=len;
		memcpy(s->block+s->used,p,n);
		s->used+=(unsigned int)n;
		p+=n;
		len-=n;
		if(s->used==SHA256_BLOCK)
		{
			Compress(s->h,s->block);
			s->used=0;
		}
	}
}

void Sha256Final(Sha256 *s,unsigned char out[SHA256_SIZE])
{
	unsigned long long bits=s->bytes*8;
	unsigned char pad=0x80;
	Sha256Update(s,&pad,1);
	pad=0;
	while(s->used!=SHA256_BLOCK-8) Sha256Update(s,&pad,1);
	unsigned char len[8];
	for(int i=0;i<8;i++) len[i]=(unsigned char)(bits>>(56-8*i));
	Sha256Update(s,len,8);
	for(int i=0;i<8;i++)
	{
		out[4*i]=(unsigned char)(s->h[i]>>24);
		out[4*i+1]=(unsigned char)(s->h[i]>>16);
		out[4*i+2]=(unsigned char)(s->h[i]>>8);
		out[4*i+3]=(unsigned char)s->h[i];
	}
}

void Sha256Hash(const void *data,size_t len,unsigned char out[SHA256_SIZE])
{
	Sha256 s;
	Sha256Init(&s);
	Sha256Update(&s,data,len);
	Sha256Final(&s,out);
}

void HmacSha256(const void *key,size_t keylen,const void *data,size_t len,unsigned char out[SHA256_SIZE])
{
	unsigned char k[SHA256_BLOCK];
	memset(k,0,sizeof(k));
	if(keylen>SHA256_BLOCK) Sha256Hash(key,keylen,k);
	else memcpy(k,key,keylen);
	unsigned char pad[SHA256_BLOCK];
	Sha256 s;
	for(int i=0;i<SHA256_BLOCK;i++) pad[i]=k[i]^0x36;
	Sha256Init(&s);
	Sha256Update(&s,pad,sizeof(pad));
	Sha256Update(&s,data,len);
	unsigned char inner[SHA256_SIZE];
	Sha256Final(&s,inner);
	for(int i=0;i<SHA256_BLOCK;i++) pad[i]=k[i]^0x5c;
	Sha256Init(&s);
	Sha256Update(&s,pad,sizeof(pad));
	Sha256Update(&s,inner,sizeof(inner));
	Sha256Final(&s,out);
}

void JoinCookie(const unsigned char secret[JOINSECRET_SIZE],unsigned int ip,unsigned short port,
	unsigned int bucket,unsigned char out[JOINCOOKIE_SIZE])
{
	unsigned char data[10];
	memcpy(data,&ip,4);      //as in sin_addr: the same bytes on every machine
	memcpy(data+4,&port,2);  //as in sin_port
	for(int i=0;i<4;i++) data[6+i]=(unsigned char)(bucket>>(8*i));
	unsigned char mac[SHA256_SIZE];
	HmacSha256(secret,JOINSECRET_SIZE,data,sizeof(data),mac);
	memcpy(out,mac,JOINCOOKIE_SIZE);
}

int JoinCookieValid(const unsigned char secret[JOINSECRET_SIZE],unsigned int ip,unsigned short port,
	unsigned int bucket,const unsigned char cookie[JOINCOOKIE_SIZE])
{
	int ok=0;
	for(int b=0;b<2;b++)
	{
		unsigned char expect[JOINCOOKIE_SIZE];
		JoinCookie(secret,ip,port,bucket-b,expect);
		unsigned char diff=0;
		for(int i=0;i<JOINCOOKIE_SIZE;i++) diff|=expect[i]^cookie[i];
		ok|=diff==0;
	}
	return ok;
}

void JoinSecretRandom(unsigned char secret[JOINSECRET_SIZE])
{
	Sha256 s;
	Sha256Init(&s);
	try
	{
		std::random_device rd;
		for(int i=0;i<16;i++)
		{
			unsigned int r=rd();
			Sha256Update(&s,&r,sizeof(r));
		}
	}
	catch(...) {}
	//never the only source, unless random_device is broken
	long long t=std::chrono::high_resolution_clock::now().time_since_epoch().count();
	time_t wall=time(0);
	const void *where=&s;
	Sha256Update(&s,&t,sizeof(t));
	Sha256Update(&s,&wall,sizeof(wall));
	Sha256Update(&s,&where,sizeof(where));
	Sha256Final(&s,secret);
}
