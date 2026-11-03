// Unit test of src/net/wire.h (roadmap M6): exact little-endian bytes, round
// trips, and readers/writers that stop at the end of their buffer. Also the M16
// messages built on it (src/net/netsync.h: INPUTCMD, SNAPSHOT, FireTick, TICKEVENT) and the second
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
	// absent: 0. No bit of it is in use (protocol 6): CAP2 and CAP2B stand for them
	{
		const int CAP2=256,CAP2B=1024;
		char hello[VERSIONTRAILER_MAX+HELLOCAPS_MAX];
		int t=WriteVersionTrailer(hello);
		const unsigned char cookie[HELLOCOOKIE_SIZE]={1,2,3,4,5,6,7,8};
		int c=WriteHelloCaps(hello+t,HELLOCAP_COOKIE|CAP2,cookie);
		CHECK(c==2+HELLOCOOKIE_SIZE&&(unsigned char)hello[t+c-1]==CAP2>>8);
		HelloCaps caps;
		ReadHelloCaps(hello,t+c,&caps);
		CHECK(caps.caps==(HELLOCAP_COOKIE|HELLOCAP_HASCOOKIE|CAP2)&&caps.cookie[7]==8);
		ReadHelloCaps(hello,t+c-1,&caps);   //the cookie and nothing after
		CHECK(caps.caps==(HELLOCAP_COOKIE|HELLOCAP_HASCOOKIE));
		ReadHelloCaps(hello,t+5,&caps);     //cut in the cookie: neither it nor caps2
		CHECK(caps.caps==HELLOCAP_COOKIE);
		c=WriteHelloCaps(hello+t,HELLOCAP_SMOKE|CAP2,0);
		CHECK(c==2);
		ReadHelloCaps(hello,t+c,&caps);
		CHECK(caps.caps==(HELLOCAP_SMOKE|CAP2));
		ReadHelloCaps(hello,t+1,&caps);
		CHECK(caps.caps==HELLOCAP_SMOKE);
		c=WriteHelloCaps(hello+t,CAP2|CAP2B,0);
		ReadHelloCaps(hello,t+c,&caps);
		CHECK(c==2&&(unsigned char)hello[t+1]==5&&caps.caps==(CAP2|CAP2B));
	}
	// phase 4: a player's state through the wire (SnapPack, SnapUnpack)
	{
		SnapState st;
		memset(&st,0,sizeof(st));
		st.pos[0]=600.25f;     //the world repeats: 88.25
		st.pos[1]=-10.5f;
		st.pos[2]=-0.5f;       //511.5
		st.vel[1]=-2.5f;       //a fast fall: a char/64 wrapped it to rising
		st.vel[0]=40;          //beyond +-32: held there
		st.vel[2]=0.0009765625f;
		st.yaw=-1.5707963f;    //a quarter turn back: 49152
		st.pitch=.3f;
		st.roll=-200;
		st.keys=MOVEKEY_UP|MOVEKEY_JETPACK;
		st.weapon=3;
		st.playstate=10;
		st.flags=SNAPFLAG_GROUND;
		SnapPlayer p;
		SnapPack(st,&p);
		CHECK(p.in==1&&p.x==(uint16_t)(88.25f*128)&&p.z==(uint16_t)(511.5f*128)&&p.y==-672);
		CHECK(p.vel[1]==-2560&&p.vel[0]==32767&&p.vel[2]==1);
		CHECK(p.yaw==49152&&p.pitch==3000&&p.roll==-127);
		SnapState o;
		SnapUnpack(p,&o);
		CHECK(o.pos[0]==88.25f&&o.pos[1]==-10.5f&&o.pos[2]==511.5f&&o.vel[1]==-2.5f);
		CHECK(fabsf(o.yaw-(6.28318530718f-1.5707963f))<1e-4f&&fabsf(o.pitch-.3f)<1e-6f);
		CHECK(o.keys==st.keys&&o.weapon==3&&o.playstate==10&&o.flags==SNAPFLAG_GROUND&&o.roll==-127);
		st.pos[0]=st.pos[1]=st.vel[0]=st.yaw=nanf("");   //not a number: 0, not undefined
		SnapPack(st,&p);
		CHECK(p.x==0&&p.y==0&&p.vel[0]==0&&p.yaw==0);
	}
	// phase 4: SNAPSHOT with the others, complete, then as deltas on a base
	{
		SnapPlayers a;
		memset(&a,0,sizeof(a));
		SnapState st;
		memset(&st,0,sizeof(st));
		st.playstate=10;
		for(int i=0;i<3;i++)
		{
			st.pos[0]=100.f+i;
			st.pos[1]=40;
			st.pos[2]=200;
			st.weapon=(unsigned char)i;
			SnapPack(st,&a.p[i==2?31:i]);   //players 0, 1 and 31
		}
		SnapshotMessage m;
		memset(&m,0,sizeof(m));
		m.type=SNAPSHOT;
		m.tick=1000;
		m.lastcmd=55;
		unsigned char buf[SNAPSHOTSELFSIZE+SNAPMAXPLAYERS*SNAPENTRYMAX];
		int n=WritePlayerSnapshot(buf,sizeof(buf),&m,a,0);
		const int head=SNAPSHOTSELFSIZE-SYNCSTATESIZE;
		CHECK(m.n==3&&n==head+3*SNAPENTRYMAX);
		// the first entry, byte for byte: pnum, mask, x z y, vel, yaw pitch, keys,
		// weapon playstate flags, roll
		const unsigned char entry[SNAPENTRYMAX]={0,SNAPF_ALL, 0x00,0x32, 0x00,0x64, 0x00,0x0A,
			0,0, 0,0, 0,0, 0,0, 0,0, 0, 0,10,0, 0};
		CHECK(buf[head-1]==3&&memcmp(buf+head,entry,sizeof(entry))==0);
		CHECK(buf[head+2*SNAPENTRYMAX]==31);
		SnapshotMessage om;
		SnapPlayers oa;
		CHECK(ReadPlayerSnapshot(buf,n,&om,&oa,0));
		CHECK(om.tick==1000&&om.basetick==0&&om.lastcmd==55&&om.n==3&&memcmp(&oa,&a,sizeof(a))==0);
		CHECK(!ReadPlayerSnapshot(buf,n-1,&om,&oa,0));   //cut short
		// its header alone, as phase 2 reads it (the base it needs)
		CHECK(ReadSnapshotSelf(buf,n,&om)&&om.n==3&&om.basetick==0);
		// nothing changed: a delta of no player at all
		m.tick=1002;
		m.basetick=1000;
		n=WritePlayerSnapshot(buf,sizeof(buf),&m,a,&a);
		CHECK(n==head&&m.n==0);
		// player 1 turns, 31 leaves, 5 comes: three entries, only what changed
		SnapPlayers b=a;
		b.p[1].yaw=1234;
		memset(&b.p[31],0,sizeof(b.p[31]));
		st.pos[0]=300;
		SnapPack(st,&b.p[5]);
		m.tick=1004;
		n=WritePlayerSnapshot(buf,sizeof(buf),&m,b,&a);
		CHECK(m.n==3&&n==head+(2+4)+SNAPENTRYMAX+2);
		CHECK(buf[head]==1&&buf[head+1]==SNAPF_VIEW&&buf[head+6]==5&&buf[head+7]==SNAPF_ALL);
		CHECK(buf[head+6+SNAPENTRYMAX]==31&&buf[head+7+SNAPENTRYMAX]==SNAPF_GONE);
		SnapPlayers ob;
		CHECK(ReadPlayerSnapshot(buf,n,&om,&ob,&a));
		CHECK(om.basetick==1000&&memcmp(&ob,&b,sizeof(b))==0);
		// the base is needed, and only for a delta
		CHECK(!ReadPlayerSnapshot(buf,n,&om,&ob,0));
		m.basetick=0;
		CHECK(WritePlayerSnapshot(buf,sizeof(buf),&m,b,&a)==0);
		m.basetick=1000;
		CHECK(WritePlayerSnapshot(buf,sizeof(buf),&m,b,0)==0);
		// a 31-player complete snapshot fits in a datagram (MAXPACKETSIZE 1000, less
		// the 3 bytes of grouping)
		SnapPlayers full;
		for(int i=0;i<SNAPMAXPLAYERS;i++) SnapPack(st,&full.p[i]);
		memset(&full.p[7],0,sizeof(full.p[7]));   //the client's own: never in it
		m.basetick=0;
		m.self=1;
		n=WritePlayerSnapshot(buf,sizeof(buf),&m,full,0);
		CHECK(n==SNAPSHOTSELFSIZE+31*SNAPENTRYMAX&&n<=997);
		m.self=0;
		// what a reader refuses (each from a good complete snapshot of players 0, 1, 31)
		m.tick=1000;
		n=WritePlayerSnapshot(buf,sizeof(buf),&m,a,0);
		unsigned char bad[sizeof(buf)];
		memcpy(bad,buf,n);
		bad[head]=32;   //no such player
		CHECK(!ReadPlayerSnapshot(bad,n,&om,&oa,0));
		memcpy(bad,buf,n);
		bad[head+SNAPENTRYMAX]=0;   //player 0 twice
		CHECK(!ReadPlayerSnapshot(bad,n,&om,&oa,0));
		memcpy(bad,buf,n);
		bad[head+1]=SNAPF_ALL|64;   //an unknown field
		CHECK(!ReadPlayerSnapshot(bad,n,&om,&oa,0));
		memcpy(bad,buf,n);
		bad[head+1]=0;   //nothing
		CHECK(!ReadPlayerSnapshot(bad,n,&om,&oa,0));
		// a new player without all its fields; one that leaves without being there
		m.tick=1002;
		m.basetick=1000;
		memset(&b,0,sizeof(b));
		n=WritePlayerSnapshot(buf,sizeof(buf),&m,b,&a);   //all three leave
		CHECK(m.n==3&&ReadPlayerSnapshot(buf,n,&om,&ob,&a)&&!ob.p[0].in&&!ob.p[31].in);
		SnapPlayers empty;
		memset(&empty,0,sizeof(empty));
		CHECK(!ReadPlayerSnapshot(buf,n,&om,&ob,&empty));
		n=WritePlayerSnapshot(buf,sizeof(buf),&m,a,&b);   //all three come back, complete
		buf[head+1]=SNAPF_POS;
		CHECK(!ReadPlayerSnapshot(buf,n,&om,&ob,&b));
	}
	// phase 4: the server's deltas against what the client acked, through losses,
	// an ack that never comes back, a base gone on either side
	{
		SnapRing sent,recv;
		memset(&sent,0,sizeof(sent));
		memset(&recv,0,sizeof(recv));
		uint32_t acked=0;
		int anyack=0,deltas=0,fulls=0,dropped=0,got=0;
		SnapPlayers cur;
		memset(&cur,0,sizeof(cur));
		SnapState st;
		memset(&st,0,sizeof(st));
		st.playstate=10;
		unsigned char buf[SNAPSHOTSELFSIZE+SNAPMAXPLAYERS*SNAPENTRYMAX];
		uint32_t newest=0;
		for(int k=1;k<=120;k++)
		{
			uint32_t tick=0xFFFFFF00u+2*k;   //across the wrap of the tick numbers
			st.pos[0]=(float)k;   //player 2 walks, player 4 stands
			SnapPack(st,&cur.p[2]);
			if(k==1) SnapPack(st,&cur.p[4]);
			// the server: on the newest acked if it still has it (as SendSnapshots)
			const SnapPlayers *base=anyack?SnapFind(&sent,acked):0;
			SnapshotMessage m;
			memset(&m,0,sizeof(m));
			m.type=SNAPSHOT;
			m.tick=tick;
			m.basetick=base?acked:0;
			int n=WritePlayerSnapshot(buf,sizeof(buf),&m,cur,base);
			CHECK(n>0);
			SnapStore(&sent,tick,cur);
			if(base) deltas++; else fulls++;
			if(base&&k>2&&k<50) CHECK(n==SNAPSHOTSELFSIZE-SYNCSTATESIZE+2+6);   //player 2's x only
			// the network: 10..19 lost; 60..99 come but the acks don't go back
			if(k>=10&&k<20) continue;
			// the client (as ClientSnapshot): the base it names, if it has it
			SnapshotMessage h;
			CHECK(ReadSnapshotSelf(buf,n,&h));
			const SnapPlayers *cbase=0;
			if(h.basetick&&!(cbase=SnapFind(&recv,h.basetick)))
				dropped++;
			else
			{
				SnapPlayers out;
				CHECK(ReadPlayerSnapshot(buf,n,&h,&out,cbase));
				CHECK(memcmp(&out,&cur,sizeof(out))==0);
				got++;
				SnapStore(&recv,tick,out);
				newest=tick;
			}
			//the client forgets (renumbered players) while its ack of 105 is lost
			if(k==105) memset(&recv,0,sizeof(recv));
			if((k>=60&&k<100)||k==105) continue;
			// the ack back to the server (as SnapAck): 0 when it has nothing
			uint32_t ack=SnapFind(&recv,newest)?newest:0;
			if(!ack) anyack=0;
			else if(!anyack||CmdNewer(ack,acked)) { acked=ack; anyack=1; }
		}
		// complete: the first; 92..100, once the ack of 59 (60..99 not acked) has left
		// the server's 32; 107, after the forgetting at 105 (106: a delta on 104 the
		// client no longer has, dropped, then its ack 0)
		CHECK(dropped==1&&got==120-10-1);
		CHECK(fulls==1+9+1&&deltas==120-fulls);
		SnapState o;
		uint32_t last=0xFFFFFF00u+240;
		// drawn between two: player 2 at 119.5 between the ticks of 119 and 120
		CHECK(SnapSample(&recv,2,last-1,0,&o)==SNAPSAMPLE_LERP&&fabsf(o.pos[0]-119.5f)<1e-3f);
		// before anything kept: nothing; past the newest: with its velocity, 6 ticks at most
		CHECK(SnapSample(&recv,2,last-60,0,&o)==SNAPSAMPLE_NONE);
		CHECK(SnapSample(&recv,3,last,0,&o)==SNAPSAMPLE_NONE);   //never there
		CHECK(SnapSample(&recv,4,last,.5f,&o)==SNAPSAMPLE_EXTRAP&&o.pos[0]==1);
	}
	// phase 4: SnapSample across the edge of the world, a teleport, a player leaving
	{
		SnapRing r;
		memset(&r,0,sizeof(r));
		SnapPlayers s;
		memset(&s,0,sizeof(s));
		SnapState st;
		memset(&st,0,sizeof(st));
		st.playstate=10;
		st.pos[0]=511.75f;
		st.vel[0]=1;   //a unit per 40 ms step
		st.yaw=3.1f;
		SnapPack(st,&s.p[1]);
		SnapPack(st,&s.p[2]);
		SnapPack(st,&s.p[3]);
		SnapStore(&r,100,s);
		st.pos[0]=.25f;   //over the edge: half a unit on
		st.yaw=-3.1f;     //and across the half turn
		SnapPack(st,&s.p[1]);
		st.pos[0]=200;    //far: a respawn
		SnapPack(st,&s.p[2]);
		memset(&s.p[3],0,sizeof(s.p[3]));   //gone
		SnapStore(&r,102,s);
		SnapState o;
		CHECK(SnapSample(&r,1,101,0,&o)==SNAPSAMPLE_LERP&&fabsf(o.pos[0]-512)<1e-3f);
		CHECK(fabsf(fabsf(o.yaw)-3.14159f)<.01f);   //the short way round, not through 0
		CHECK(SnapSample(&r,2,101,0,&o)==SNAPSAMPLE_HOLD&&fabsf(o.pos[0]-511.75f)<1e-3f);
		CHECK(SnapSample(&r,2,102,0,&o)==SNAPSAMPLE_EXTRAP&&o.pos[0]==200);
		CHECK(SnapSample(&r,3,101,.9f,&o)==SNAPSAMPLE_HOLD);
		CHECK(SnapSample(&r,3,102,0,&o)==SNAPSAMPLE_NONE);
		// extrapolated 100 ms at most: 6 ticks of 1/1024-rounded velocity
		CHECK(SnapSample(&r,1,120,0,&o)==SNAPSAMPLE_EXTRAP&&fabsf(o.pos[0]-(.25f+6*SNAPSTEPSPERTICK))<1e-3f);
		// a snapshot older than all the kept ones, once full, isn't kept
		for(uint32_t t=104;t<104+2*SNAPRING;t+=2) SnapStore(&r,t,s);
		SnapStore(&r,50,s);
		CHECK(!SnapFind(&r,50)&&!SnapFind(&r,100)&&SnapFind(&r,104+2*SNAPRING-2));
	}

	// phase 5: what follows FIREGUN from a client (FireTick), exact bytes
	{
		unsigned char m[FIRETICKAT+FIRETICKSIZE+4];
		memset(m,0xEE,sizeof(m));
		FireTick ft={0x01020304u,0x80,0x0A0B0C0Du,0x1234};
		CHECK(WriteFireTick(m+FIRETICKAT,FIRETICKSIZE,&ft)==FIRETICKSIZE);
		const unsigned char ex[FIRETICKSIZE]={4,3,2,1, 0x80, 0x0D,0x0C,0x0B,0x0A, 0x34,0x12};
		CHECK(!memcmp(m+FIRETICKAT,ex,sizeof(ex))&&m[FIRETICKAT-1]==0xEE);
		CHECK(!WriteFireTick(m,FIRETICKSIZE-1,&ft));
		FireTick back;
		CHECK(ReadFireTick(m+FIRETICKAT,FIRETICKSIZE,&back)&&back.viewtick==ft.viewtick&&back.viewfrac==0x80&&
			back.cmd==ft.cmd&&back.objid==0x1234);
		CHECK(!ReadFireTick(m+FIRETICKAT,FIRETICKSIZE-1,&back));   //cut short: an older client's 27 bytes
		CHECK(!ReadFireTick(m+FIRETICKAT,0,&back));
		// relayed by the server: the tick after the FIREGUN bytes; not there from an older one
		uint32_t tick=0;
		WirePutU32(m+FIRETICKAT,123456u);
		CHECK(ReadFireRelayTick(m,FIRETICKAT+FIRERELAYSIZE,&tick)&&tick==123456u);
		CHECK(!ReadFireRelayTick(m,FIRETICKAT,&tick)&&!ReadFireRelayTick(m,FIRETICKAT+3,&tick));
	}
	// phase 5: TICKEVENT, each kind through the wire, one byte short refused
	{
		unsigned char m[TICKEVENTMAX+8];
		TickEvent e,back;
		const int sizes[6]={0,1+4+1+3+12,1+4+1+3,1+4+1+3+24+2,1+4+1+3+12,1+4+1+4};
		for(int kind=TE_KILLED;kind<=TE_SMOKE;kind++)
		{
			memset(&e,0,sizeof(e));
			e.type=TICKEVENT;
			e.tick=0xFFFFFFF0u+kind;
			e.kind=(unsigned char)kind;
			e.player=3;e.other=31;e.haspos=1;e.num=0x8001;e.ms=2999;
			for(int k=0;k<3;k++) { e.pos[k]=100.5f+k; e.vel[k]=-1.25f*k; }
			int n=WriteTickEvent(m,sizeof(m),&e);
			CHECK(n==sizes[kind]&&n<=TICKEVENTMAX&&m[0]==TICKEVENT&&m[5]==kind);
			CHECK(ReadTickEvent(m,n,&back)&&back.tick==e.tick&&back.kind==kind&&back.player==3);
			if(kind==TE_KILLED) CHECK(back.other==31&&back.haspos==1&&back.pos[2]==102.5f);
			if(kind==TE_PAIN) CHECK(back.num==0x8001&&back.pos[0]==0);
			if(kind==TE_GRENADE) CHECK(back.num==0x8001&&back.vel[2]==-2.5f&&back.ms==2999&&back.pos[1]==101.5f);
			if(kind==TE_EXPLODE) CHECK(back.num==0x8001&&back.pos[0]==100.5f&&back.vel[2]==0);
			if(kind==TE_SMOKE) CHECK(back.num==0x8001&&back.other==31);
			CHECK(!ReadTickEvent(m,n-1,&back));
		}
		// KILLED without a position: 3 bytes after the kind
		e.kind=TE_KILLED;
		e.haspos=0;
		int n=WriteTickEvent(m,sizeof(m),&e);
		CHECK(n==1+4+1+3&&ReadTickEvent(m,n,&back)&&!back.haspos&&back.pos[0]==0);
		m[8]=2;   //haspos must be 0 or 1
		CHECK(!ReadTickEvent(m,sizeof(m),&back));
		e.kind=0;
		CHECK(!WriteTickEvent(m,sizeof(m),&e));
		m[5]=6;   //an unknown kind
		CHECK(!ReadTickEvent(m,sizeof(m),&back));
		m[5]=0;
		CHECK(!ReadTickEvent(m,sizeof(m),&back));
	}

	if(failures) { printf("%d failure(s)\n",failures); return 1; }
	printf("wire: all checks passed\n");
	return 0;
}
