//SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104), self-contained: the build without
//GameNetworkingSockets has no crypto library. Used for the join cookie of the
//server (roadmap M11 phase 2, docs/M11.md). Tested by tests/hmac_test.cpp against
//the FIPS 180-2 and RFC 4231 vectors.

#ifndef HMACH
#define HMACH

#include <stddef.h>

#define SHA256_SIZE 32
#define SHA256_BLOCK 64

struct Sha256
{
	unsigned int h[8];
	unsigned char block[SHA256_BLOCK];
	unsigned int used;        //bytes in block
	unsigned long long bytes; //total hashed
};

void Sha256Init(Sha256 *s);
void Sha256Update(Sha256 *s,const void *data,size_t len);
void Sha256Final(Sha256 *s,unsigned char out[SHA256_SIZE]);
void Sha256Hash(const void *data,size_t len,unsigned char out[SHA256_SIZE]);
void HmacSha256(const void *key,size_t keylen,const void *data,size_t len,unsigned char out[SHA256_SIZE]);

//---- join cookie (server, REQUESTENTER) ----
//cookie = HMAC-SHA256(secret, ip (4 bytes as on the wire) | port (2, network order)
//| bucket (uint32 little-endian)), first JOINCOOKIE_SIZE bytes (= HELLOCOOKIE_SIZE, version.h).
#define JOINSECRET_SIZE 32
#define JOINCOOKIE_SIZE 8
void JoinCookie(const unsigned char secret[JOINSECRET_SIZE],unsigned int ip,unsigned short port,
	unsigned int bucket,unsigned char out[JOINCOOKIE_SIZE]);
//1 if cookie is the one of bucket or bucket-1 (constant time compare)
int JoinCookieValid(const unsigned char secret[JOINSECRET_SIZE],unsigned int ip,unsigned short port,
	unsigned int bucket,const unsigned char cookie[JOINCOOKIE_SIZE]);
//fills secret with random bytes (std::random_device, mixed with the clock and
//addresses through SHA-256 in case random_device is weak)
void JoinSecretRandom(unsigned char secret[JOINSECRET_SIZE]);

#endif
