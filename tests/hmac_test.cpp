// Unit test of src/net/hmac.cpp (roadmap M11 phase 2): SHA-256 against the
// FIPS 180-2 examples, HMAC-SHA256 against the RFC 4231 test cases 1 to 7, and
// the join cookie (depends on secret, IP, port and time bucket; the previous
// bucket is accepted, older ones are not).
#include "hmac.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); failures++; } } while(0)

static void Hex(const char *hex,unsigned char *out,int n)
{
	for(int i=0;i<n;i++)
	{
		unsigned int v;
		sscanf(hex+2*i,"%2x",&v);
		out[i]=(unsigned char)v;
	}
}

static int Same(const unsigned char *got,const char *hex,int n)
{
	unsigned char want[64];
	Hex(hex,want,n);
	if(memcmp(got,want,n)==0) return 1;
	printf("  got  ");
	for(int i=0;i<n;i++) printf("%02x",got[i]);
	printf("\n  want %s\n",hex);
	return 0;
}

static void CheckSha(const char *msg,const char *hex)
{
	unsigned char out[SHA256_SIZE];
	Sha256Hash(msg,strlen(msg),out);
	CHECK(Same(out,hex,32));
}

static void CheckHmac(const unsigned char *key,int keylen,const void *data,int len,const char *hex,int n=32)
{
	unsigned char out[SHA256_SIZE];
	HmacSha256(key,keylen,data,len,out);
	CHECK(Same(out,hex,n));
}

int main()
{
	// FIPS 180-2, appendix B
	CheckSha("","e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CheckSha("abc","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CheckSha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	{
		// a million 'a', fed in uneven pieces (block boundaries inside updates)
		Sha256 s;
		Sha256Init(&s);
		char a[1000];
		memset(a,'a',sizeof(a));
		size_t left=1000000,piece=1;
		while(left)
		{
			size_t n=piece<left?piece:left;
			if(n>sizeof(a)) n=sizeof(a);
			Sha256Update(&s,a,n);
			left-=n;
			piece=piece*7%997+1;
		}
		unsigned char out[SHA256_SIZE];
		Sha256Final(&s,out);
		CHECK(Same(out,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",32));
	}
	// 55 to 65 'x' (values from Python's hashlib): the padding just fits, just
	// doesn't, a whole block, one byte more
	{
		struct { int n; const char *hex; } pads[]={
			{55,"d5e285683cd4efc02d021a5c62014694958901005d6f71e89e0989fac77e4072"},
			{56,"04c26261370ee7541549d16dee320c723e3fd14671e66a099afe0a377c16888e"},
			{63,"75220b47218278e656f2013bb8f0c455a25eaf01e86c64924e9d48d89776d6f2"},
			{64,"7ce100971f64e7001e8fe5a51973ecdfe1ced42befe7ee8d5fd6219506b5393c"},
			{65,"9537c5fdf120482f7d58d25e9ed583f52c02b4e304ea814db1633ad565aed7e9"}};
		for(int p=0;p<5;p++)
		{
			char m[66];
			memset(m,'x',pads[p].n);
			m[pads[p].n]=0;
			CheckSha(m,pads[p].hex);
		}
	}

	// RFC 4231
	unsigned char key[131];
	unsigned char data[160];
	memset(key,0x0b,20);
	CheckHmac(key,20,"Hi There",8,"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
	CheckHmac((const unsigned char *)"Jefe",4,"what do ya want for nothing?",28,
		"5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
	memset(key,0xaa,20);
	memset(data,0xdd,50);
	CheckHmac(key,20,data,50,"773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
	for(int i=0;i<25;i++) key[i]=(unsigned char)(i+1);
	memset(data,0xcd,50);
	CheckHmac(key,25,data,50,"82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
	memset(key,0x0c,20);
	CheckHmac(key,20,"Test With Truncation",20,"a3b6167473100ee06e0c796c2955552b",16);
	memset(key,0xaa,131);
	const char *t6="Test Using Larger Than Block-Size Key - Hash Key First";
	CheckHmac(key,131,t6,(int)strlen(t6),"60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
	const char *t7="This is a test using a larger than block-size key and a larger than block-size data."
		" The key needs to be hashed before being used by the HMAC algorithm.";
	CheckHmac(key,131,t7,(int)strlen(t7),"9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2");

	// join cookie
	unsigned char secret[JOINSECRET_SIZE],other[JOINSECRET_SIZE];
	JoinSecretRandom(secret);
	JoinSecretRandom(other);
	CHECK(memcmp(secret,other,JOINSECRET_SIZE)!=0);
	unsigned int ip=0x0100007f;
	unsigned short port=0x3930;
	unsigned char c[JOINCOOKIE_SIZE],c2[JOINCOOKIE_SIZE];
	JoinCookie(secret,ip,port,1000,c);
	JoinCookie(secret,ip,port,1000,c2);
	CHECK(memcmp(c,c2,JOINCOOKIE_SIZE)==0);                    // stateless: the same again
	CHECK(JoinCookieValid(secret,ip,port,1000,c));
	CHECK(JoinCookieValid(secret,ip,port,1001,c));             // previous bucket
	CHECK(!JoinCookieValid(secret,ip,port,1002,c));            // expired
	CHECK(!JoinCookieValid(secret,ip,port,999,c));             // from the future
	CHECK(!JoinCookieValid(secret,ip+1,port,1000,c));          // other IP
	CHECK(!JoinCookieValid(secret,ip,port+1,1000,c));          // other port
	CHECK(!JoinCookieValid(other,ip,port,1000,c));             // other server run
	c2[JOINCOOKIE_SIZE-1]^=1;
	CHECK(!JoinCookieValid(secret,ip,port,1000,c2));
	JoinCookie(secret,ip,port,0,c);                            // bucket 0 - 1 wraps
	CHECK(JoinCookieValid(secret,ip,port,0,c));
	CHECK(JoinCookieValid(secret,ip,port,1,c));

	if(failures) printf("%d failure(s)\n",failures);
	else printf("hmac_test: all OK\n");
	return failures?1:0;
}
