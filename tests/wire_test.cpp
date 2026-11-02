// Unit test of src/net/wire.h (roadmap M6): exact little-endian bytes, round
// trips, and readers/writers that stop at the end of their buffer. Also the M16
// messages built on it (src/net/netsync.h: INPUTCMD, SNAPSHOT) and the second
// capability byte of REQUESTENTER (src/net/version.cpp).
#include "wire.h"
#include "netsync.h"
#include "version.h"
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

	// M16 (src/net/netsync.h): INPUTCMD, exact bytes and round trip
	{
		InputCmdMessage m;
		memset(&m,0,sizeof(m));
		m.type=INPUTCMD;
		m.n=2;
		m.firstcmd=0x01020304;
		m.acksnap=7;
		m.cmd[0].keys=MOVEKEY_UP|MOVEKEY_PAD;
		m.cmd[0].padfwd=-127;
		m.cmd[0].padstrafe=5;
		m.cmd[0].yaw=0xABCD;
		m.cmd[0].pitch=-2;
		m.cmd[0].weapon=3;
		m.cmd[0].flags=MOVECMD_ALIVE;
		m.cmd[0].msec=17;
		m.cmd[1]=m.cmd[0];
		m.cmd[1].msec=16;
		unsigned char ib[64];
		int n=WriteInputCmd(ib,sizeof(ib),&m);
		CHECK(n==INPUTCMDHEADER+2*MOVECMDSIZE);
		const unsigned char head[]={INPUTCMD,2, 4,3,2,1, 7,0,0,0,
			MOVEKEY_UP|MOVEKEY_PAD,0x81,5, 0xCD,0xAB, 0xFE,0xFF, 3,1,17};
		CHECK(memcmp(ib,head,sizeof(head))==0);
		InputCmdMessage o;
		CHECK(ReadInputCmd(ib,n,&o));
		CHECK(o.n==2&&o.firstcmd==0x01020304&&o.acksnap==7);
		CHECK(o.cmd[0].padfwd==-127&&o.cmd[0].pitch==-2&&o.cmd[0].yaw==0xABCD&&o.cmd[1].msec==16);
		// cut short, no command, or more than 4: dropped
		CHECK(!ReadInputCmd(ib,n-1,&o));
		ib[1]=0;
		CHECK(!ReadInputCmd(ib,n,&o));
		ib[1]=5;
		CHECK(!ReadInputCmd(ib,sizeof(ib),&o));
		m.n=5;
		CHECK(WriteInputCmd(ib,sizeof(ib),&m)==0);
		// serial numbers across the wrap
		CHECK(CmdNewer(1,0xFFFFFFFFu)&&!CmdNewer(0xFFFFFFFFu,1)&&!CmdNewer(5,5));
	}
	// SNAPSHOT with only our own player (phase 2): floats exact
	{
		SnapshotMessage s;
		memset(&s,0,sizeof(s));
		s.type=SNAPSHOT;
		s.tick=100;
		s.lastcmd=0xFFFFFFF0u;
		s.self=1;
		s.state.pos[0]=700.123456f;
		s.state.vel[1]=-1e-7f;
		s.state.thrustfuel=.5f;
		s.state.speedpenalty=1.1f;
		s.state.flags=SYNCSTATE_GLIDING|SYNCSTATE_JUMP;
		s.state.playstate=10;
		s.state.life=-5;
		unsigned char sb[SNAPSHOTSELFSIZE+4];
		int n=WriteSnapshotSelf(sb,sizeof(sb),&s);
		CHECK(n==SNAPSHOTSELFSIZE);
		SnapshotMessage o;
		memset(&o,0x55,sizeof(o));
		CHECK(ReadSnapshotSelf(sb,n,&o));
		CHECK(o.tick==100&&o.basetick==0&&o.lastcmd==0xFFFFFFF0u&&o.self==1&&o.n==0);
		CHECK(o.state.pos[0]==700.123456f&&o.state.vel[1]==-1e-7f&&o.state.thrustfuel==.5f&&o.state.speedpenalty==1.1f);
		CHECK(o.state.flags==(SYNCSTATE_GLIDING|SYNCSTATE_JUMP)&&o.state.playstate==10&&o.state.life==-5);
		CHECK(!ReadSnapshotSelf(sb,n-1,&o));
		s.self=0;
		CHECK(WriteSnapshotSelf(sb,sizeof(sb),&s)==SNAPSHOTSELFSIZE-SYNCSTATESIZE);
	}
	// the second capability byte (version.h): after the cookie, or after caps;
	// absent (a 0.7 build): 0
	{
		char hello[VERSIONTRAILER_MAX+HELLOCAPS_MAX];
		int t=WriteVersionTrailer(hello);
		const unsigned char cookie[HELLOCOOKIE_SIZE]={1,2,3,4,5,6,7,8};
		int c=WriteHelloCaps(hello+t,HELLOCAP_COOKIE|HELLOCAP_INPUTS,cookie);
		CHECK(c==2+HELLOCOOKIE_SIZE&&(unsigned char)hello[t+c-1]==HELLOCAP_INPUTS>>8);
		HelloCaps caps;
		ReadHelloCaps(hello,t+c,&caps);
		CHECK(caps.caps==(HELLOCAP_COOKIE|HELLOCAP_HASCOOKIE|HELLOCAP_INPUTS)&&caps.cookie[7]==8);
		ReadHelloCaps(hello,t+c-1,&caps);   //0.7: the cookie and nothing after
		CHECK(caps.caps==(HELLOCAP_COOKIE|HELLOCAP_HASCOOKIE));
		ReadHelloCaps(hello,t+5,&caps);     //cut in the cookie: neither it nor caps2
		CHECK(caps.caps==HELLOCAP_COOKIE);
		c=WriteHelloCaps(hello+t,HELLOCAP_SMOKE|HELLOCAP_INPUTS,0);
		CHECK(c==2);
		ReadHelloCaps(hello,t+c,&caps);
		CHECK(caps.caps==(HELLOCAP_SMOKE|HELLOCAP_INPUTS));
		ReadHelloCaps(hello,t+1,&caps);
		CHECK(caps.caps==HELLOCAP_SMOKE);
	}

	if(failures) { printf("%d failure(s)\n",failures); return 1; }
	printf("wire: all checks passed\n");
	return 0;
}
