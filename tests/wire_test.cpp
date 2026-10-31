// Unit test of src/net/wire.h (roadmap M6): exact little-endian bytes, round
// trips, and readers/writers that stop at the end of their buffer.
#include "wire.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); failures++; } } while(0)

int main()
{
	unsigned char buf[64];
	memset(buf,0xAA,sizeof(buf));

	// exact bytes
	ByteWriter w(buf,sizeof(buf));
	w.U8(0x12);
	w.U16(0x3456);
	w.U32(0x789ABCDEu);
	w.I32(-2);
	w.F32(1.0f);
	w.Str("ab",4);
	CHECK(w.Ok());
	CHECK(w.Size()==1+2+4+4+4+4);
	const unsigned char expect[]={0x12, 0x56,0x34, 0xDE,0xBC,0x9A,0x78, 0xFE,0xFF,0xFF,0xFF,
		0x00,0x00,0x80,0x3F, 'a','b',0,0};
	CHECK(memcmp(buf,expect,sizeof(expect))==0);

	// round trip
	ByteReader r(buf,w.Size());
	CHECK(r.U8()==0x12);
	CHECK(r.U16()==0x3456);
	CHECK(r.U32()==0x789ABCDEu);
	CHECK(r.I32()==-2);
	CHECK(r.F32()==1.0f);
	char s[4];
	r.Str(s,4);
	CHECK(strcmp(s,"ab")==0);
	CHECK(r.Ok());
	CHECK(r.Left()==0);

	// reading past the end: invalid, zeros, and it stays invalid
	r.U8();
	CHECK(!r.Ok());
	ByteReader r2(buf,3);
	CHECK(r2.U16()==0x5612);
	CHECK(r2.U32()==0);
	CHECK(!r2.Ok());
	CHECK(r2.U8()==0);
	CHECK(r2.Left()==0);
	ByteReader r3(buf,5);
	char big[8];
	memset(big,'x',sizeof(big));
	r3.Bytes(big,8);
	CHECK(!r3.Ok()&&big[0]==0&&big[7]==0);
	ByteReader r4(buf,-5);   // negative size reads nothing
	r4.U8();
	CHECK(!r4.Ok());

	// fixed-size string: NUL forced, nothing read beyond its field
	unsigned char str[]={'h','e','l','l','o','!',9};
	ByteReader r5(str,sizeof(str));
	char name[6];
	r5.Str(name,6);
	CHECK(strcmp(name,"hello")==0);
	CHECK(r5.U8()==9);
	CHECK(r5.Ok());

	// writing past the end: nothing written outside, invalid
	unsigned char small[6];
	memset(small,0,sizeof(small));
	unsigned char guard=small[5];
	ByteWriter w2(small,5);
	w2.U32(1);
	w2.U16(2);   // doesn't fit
	CHECK(!w2.Ok());
	CHECK(w2.Size()==4);
	CHECK(small[4]==0&&small[5]==guard);
	w2.U8(3);    // stays invalid
	CHECK(w2.Size()==4);

	// the by-reference forms used by the Sync* templates
	struct T { char a; unsigned char b; uint16_t c; uint32_t d; int e; float f; char g[5]; } in,out;
	memset(&in,0,sizeof(in));
	in.a=-3; in.b=200; in.c=65535; in.d=4000000000u; in.e=-123456; in.f=-0.5f; strcpy(in.g,"abcd");
	ByteWriter w3(buf,sizeof(buf));
	w3.i8(in.a); w3.u8(in.b); w3.u16(in.c); w3.u32(in.d); w3.i32(in.e); w3.f32(in.f); w3.str(in.g,5);
	CHECK(w3.Ok()&&w3.Size()==1+1+2+4+4+4+5);
	memset(&out,0x55,sizeof(out));
	ByteReader r6(buf,w3.Size());
	r6.i8(out.a); r6.u8(out.b); r6.u16(out.c); r6.u32(out.d); r6.i32(out.e); r6.f32(out.f); r6.str(out.g,5);
	CHECK(r6.Ok());
	CHECK(out.a==-3&&out.b==200&&out.c==65535&&out.d==4000000000u&&out.e==-123456&&out.f==-0.5f);
	CHECK(strcmp(out.g,"abcd")==0);

	// raw helpers
	unsigned char raw[4];
	WirePutU32(raw,0x01020304);
	CHECK(raw[0]==4&&raw[3]==1&&WireGetU32(raw)==0x01020304);
	WirePutU16(raw,0xBEEF);
	CHECK(raw[0]==0xEF&&WireGetU16(raw)==0xBEEF);

	if(failures) { printf("%d failure(s)\n",failures); return 1; }
	printf("wire: all checks passed\n");
	return 0;
}
