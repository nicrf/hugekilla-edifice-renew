//The messages of the server-authoritative movement (docs/M16.md, protocol 6): the
//client's numbered input commands (INPUTCMD) and the server's snapshots (SNAPSHOT):
//the exact state of the client's own player, and the other players, timestamped, as
//a delta on one the client acked; the shots at the tick seen and the events at a
//tick (phase 5). Apart from netmsg.h, on wire.h alone, so tests/wire_test.cpp and the
//movement code use them without the game.

#ifndef NETSYNCH
#define NETSYNCH

#include "wire.h"

#define INPUTCMD 21   //client to server, unreliable, every tick
#define SNAPSHOT 22   //server to client, unreliable, 30 Hz
//SIMPLE message, at the join: the client is in the game, the server moves it from its
//commands from now on and sends it SNAPSHOT and TICKEVENT; msg 0, msg2 = the server's
//tick
#define NETSYNCMODE 41

//one tick of a player's input
struct MoveCmd
{
	unsigned char keys;            //MOVEKEY_*
	signed char padfwd,padstrafe;  //the stick, -127..127 (MOVEKEY_PAD)
	unsigned short yaw;            //heading (fwd), 0..65535 a turn
	short pitch;                   //tilt, 1/10000 radian
	unsigned char weapon;
	unsigned char flags;           //MOVECMD_ALIVE
	unsigned char msec;            //16 or 17: the tick's length
};
#define MOVEKEY_UP 1
#define MOVEKEY_DOWN 2
#define MOVEKEY_LEFT 4
#define MOVEKEY_RIGHT 8
#define MOVEKEY_JETPACK 16
#define MOVEKEY_GLIDE 32      //the glider's key, held
#define MOVEKEY_JUMP 64       //a jump waiting for the ground (Player::jump)
#define MOVEKEY_PAD 128       //walks with the stick (padfwd, padstrafe)
#define MOVECMD_ALIVE 1       //the player is alive (playstate 10): asks to respawn when dead
#define MOVECMDSIZE 10

template<class IO> inline void SyncMoveCmd(IO &io,MoveCmd &c)
{
	io.u8(c.keys);
	io.i8((char &)c.padfwd);
	io.i8((char &)c.padstrafe);
	io.u16(c.yaw);
	io.u16((uint16_t &)c.pitch);
	io.u8(c.weapon);
	io.u8(c.flags);
	io.u8(c.msec);
}

//INPUTCMD: uint8 type, uint8 n (1..4, oldest first: the last ones again, for the
//losses), uint32 firstcmd (its number: the client's tick), uint32 acksnap (the
//newest SNAPSHOT tick the client has, the base of the next delta; 0: none), n
//commands
#define INPUTCMDMAX 4
struct InputCmdMessage
{
	char type;
	unsigned char n;
	uint32_t firstcmd;
	uint32_t acksnap;
	MoveCmd cmd[INPUTCMDMAX];
};
template<class IO> inline void SyncInputCmd(IO &io,InputCmdMessage &m)
{
	io.i8(m.type);
	io.u8(m.n);
	io.u32(m.firstcmd);
	io.u32(m.acksnap);
	for(int i=0;i<m.n&&i<INPUTCMDMAX;i++) SyncMoveCmd(io,m.cmd[i]);
}
#define INPUTCMDHEADER 10
inline int WriteInputCmd(void *buf,int size,InputCmdMessage *m)
{
	if(m->n<1||m->n>INPUTCMDMAX) return 0;
	ByteWriter w(buf,size);
	SyncInputCmd(w,*m);
	return w.Ok()?w.Size():0;
}
inline int ReadInputCmd(const void *buf,int len,InputCmdMessage *m)
{
	ByteReader r(buf,len);
	m->n=0;
	SyncInputCmd(r,*m);   //stops at n: a bad n reads nothing after the header
	return r.Ok()&&m->n>=1&&m->n<=INPUTCMDMAX;
}

//a player's state as the server simulated it, exact (the client compares to a
//thousandth of a unit and starts again from it)
struct SyncState
{
	float pos[3],vel[3];
	float thrustfuel,roll,runtime;
	float speedpenalty;            //what it carries slows it (GetSOSpeedPenalty): the
	                               //client predicts with the server's
	unsigned char flags;           //SYNCSTATE_*
	unsigned char playstate;
	short life;
};
#define SYNCSTATE_GLIDING 1
#define SYNCSTATE_GROUND 2      //informative
#define SYNCSTATE_UNDERWATER 4  //informative
#define SYNCSTATE_JUMP 8        //a jump waiting for the ground (Player::jump)
#define SYNCSTATESIZE 44
template<class IO> inline void SyncSyncState(IO &io,SyncState &s)
{
	for(int i=0;i<3;i++) io.f32(s.pos[i]);
	for(int i=0;i<3;i++) io.f32(s.vel[i]);
	io.f32(s.thrustfuel);
	io.f32(s.roll);
	io.f32(s.runtime);
	io.f32(s.speedpenalty);
	io.u8(s.flags);
	io.u8(s.playstate);
	io.u16((uint16_t &)s.life);
}

//SNAPSHOT: uint8 type, uint32 tick (the server's), uint32 basetick (0: complete),
//uint32 lastcmd (this client's last command run), uint8 self (1: the exact state of
//its player follows, SYNCSTATESIZE bytes), uint8 n (other players: phase 4, below)
struct SnapshotMessage
{
	char type;
	uint32_t tick;
	uint32_t basetick;
	uint32_t lastcmd;
	unsigned char self;
	SyncState state;
	unsigned char n;
};
template<class IO> inline void SyncSnapshotSelf(IO &io,SnapshotMessage &m)
{
	io.i8(m.type);
	io.u32(m.tick);
	io.u32(m.basetick);
	io.u32(m.lastcmd);
	io.u8(m.self);
	if(m.self) SyncSyncState(io,m.state);
	io.u8(m.n);
}
#define SNAPSHOTSELFSIZE (1+4+4+4+1+SYNCSTATESIZE+1)
inline int WriteSnapshotSelf(void *buf,int size,SnapshotMessage *m)
{
	ByteWriter w(buf,size);
	SyncSnapshotSelf(w,*m);
	return w.Ok()?w.Size():0;
}
//the header and the state of our player; the other players after it are read by
//ReadPlayerSnapshot (which needs the base basetick names)
inline int ReadSnapshotSelf(const void *buf,int len,SnapshotMessage *m)
{
	ByteReader r(buf,len);
	SyncSnapshotSelf(r,*m);
	return r.Ok();
}

//command numbers go round in 32 bits: a is after b
inline int CmdNewer(uint32_t a,uint32_t b)
{
	return (int32_t)(a-b)>0;
}

//---- phase 4: the other players, timestamped, as a delta (docs/M16.md) ----
//
//After n (SNAPSHOT above), n entries: uint8 pnum, uint8 mask (SNAPF_*), then the
//fields of the mask, in this order:
//  SNAPF_POS   uint16 x, uint16 z (1/128 unit on SNAPWORLD: round the world), int16 y (1/64)
//  SNAPF_VEL   int16 vel[3], 1/1024 unit per 40 ms step (+-32: no wrap any more)
//  SNAPF_VIEW  uint16 yaw, int16 pitch (as MoveCmd)
//  SNAPF_KEYS  uint8 keys (MOVEKEY_*, for the animations: legs, jetpack)
//  SNAPF_STATE uint8 weapon, uint8 playstate, uint8 flags (SNAPFLAG_*)
//  SNAPF_ROLL  int8 roll (the glider's, about degrees)
//  SNAPF_GONE  alone: the player left the snapshot (left, joining)
//A complete snapshot (basetick 0) has every player in it with SNAPF_ALL; a delta
//on basetick (a snapshot the client said it has: INPUTCMD acksnap) has the players
//that changed since, with the fields that changed, and SNAPF_GONE for those of the
//base that aren't in it any more. Never the client's own player (predicted, phase 3).

#include <math.h>
#include <string.h>

#define SNAPMAXPLAYERS 32      //MAXPLAYERS (multiplay.h)
#define SNAPWORLD 512          //LANDGRIDSIZE (land.h): x and z wrap on it
#define SNAPRATE 2             //ticks between two snapshots (sv_snaprate): 30 Hz
#define SNAPINTERP 6           //ticks the others are drawn in the past (cl_interp): 100 ms
#define SNAPEXTRAP 6           //ticks of extrapolation past the newest, at most: 100 ms
#define SNAPTELEPORT 8.0f      //units between two snapshots not interpolated (a respawn)
#define SNAPSTEPSPERTICK (1000.f/60/40)   //vel is per 40 ms step, a tick is 16.67 ms

#define SNAPF_POS 1
#define SNAPF_VEL 2
#define SNAPF_VIEW 4
#define SNAPF_KEYS 8
#define SNAPF_STATE 16
#define SNAPF_ROLL 32
#define SNAPF_ALL 63
#define SNAPF_GONE 128
#define SNAPENTRYMAX (2+6+6+4+1+3+1)   //an entry with every field

#define SNAPFLAG_GLIDING 1     //hanggliding
#define SNAPFLAG_GROUND 2      //on the ground (the legs walk)
#define SNAPFLAG_THRUST 4      //the jetpack pushes (keys and fuel)

//a player in a snapshot, as it goes on the wire
struct SnapPlayer
{
	unsigned char in;        //in the snapshot; the rest is 0 if not
	unsigned char keys;
	unsigned char weapon,playstate,flags;
	signed char roll;
	uint16_t x,z;
	short y;
	short vel[3];
	uint16_t yaw;
	short pitch;
};
struct SnapPlayers
{
	SnapPlayer p[SNAPMAXPLAYERS];
};

//the fields of a player that differ from base (SNAPF_ALL if it isn't in base)
inline int SnapMask(const SnapPlayer &c,const SnapPlayer &base)
{
	if(!base.in) return SNAPF_ALL;
	int m=0;
	if(c.x!=base.x||c.z!=base.z||c.y!=base.y) m|=SNAPF_POS;
	if(c.vel[0]!=base.vel[0]||c.vel[1]!=base.vel[1]||c.vel[2]!=base.vel[2]) m|=SNAPF_VEL;
	if(c.yaw!=base.yaw||c.pitch!=base.pitch) m|=SNAPF_VIEW;
	if(c.keys!=base.keys) m|=SNAPF_KEYS;
	if(c.weapon!=base.weapon||c.playstate!=base.playstate||c.flags!=base.flags) m|=SNAPF_STATE;
	if(c.roll!=base.roll) m|=SNAPF_ROLL;
	return m;
}

template<class IO> inline void SyncSnapFields(IO &io,SnapPlayer &p,int mask)
{
	if(mask&SNAPF_POS) { io.u16(p.x); io.u16(p.z); io.u16((uint16_t &)p.y); }
	if(mask&SNAPF_VEL) for(int k=0;k<3;k++) io.u16((uint16_t &)p.vel[k]);
	if(mask&SNAPF_VIEW) { io.u16(p.yaw); io.u16((uint16_t &)p.pitch); }
	if(mask&SNAPF_KEYS) io.u8(p.keys);
	if(mask&SNAPF_STATE) { io.u8(p.weapon); io.u8(p.playstate); io.u8(p.flags); }
	if(mask&SNAPF_ROLL) io.i8((char &)p.roll);
}

//a whole SNAPSHOT: m's header and our state (m->n is set), then the players of cur,
//complete if base is 0 (m->basetick must then be 0), else as a delta on base (the
//snapshot of m->basetick). 0 if it doesn't fit.
inline int WritePlayerSnapshot(void *buf,int size,SnapshotMessage *m,const SnapPlayers &cur,const SnapPlayers *base)
{
	static const SnapPlayer none={};
	if((m->basetick!=0)!=(base!=0)) return 0;
	unsigned char mask[SNAPMAXPLAYERS];
	int n=0;
	for(int i=0;i<SNAPMAXPLAYERS;i++)
	{
		const SnapPlayer &b=base?base->p[i]:none;
		mask[i]=(unsigned char)(cur.p[i].in?SnapMask(cur.p[i],b):b.in?SNAPF_GONE:0);
		if(mask[i]) n++;
	}
	m->n=(unsigned char)n;
	ByteWriter w(buf,size);
	SyncSnapshotSelf(w,*m);
	for(int i=0;i<SNAPMAXPLAYERS;i++)
	{
		if(!mask[i]) continue;
		w.U8(i);
		w.U8(mask[i]);
		if(mask[i]!=SNAPF_GONE) SyncSnapFields(w,(SnapPlayer &)cur.p[i],mask[i]);
	}
	return w.Ok()?w.Size():0;
}

//reads a SNAPSHOT into m and its players into out: from nothing if m->basetick is
//0, else from base, the snapshot of m->basetick (ReadSnapshotSelf first tells which
//one; a client that hasn't it ignores the players). 0 if anything is off: a player
//number out of range or twice, an empty or unknown mask, a new player without every
//field, one that leaves without being there, or cut short.
inline int ReadPlayerSnapshot(const void *buf,int len,SnapshotMessage *m,SnapPlayers *out,const SnapPlayers *base)
{
	ByteReader r(buf,len);
	m->n=0;
	SyncSnapshotSelf(r,*m);
	if(!r.Ok()||(m->basetick!=0)!=(base!=0)) return 0;
	if(base) *out=*base;
	else memset(out,0,sizeof(*out));
	unsigned char seen[SNAPMAXPLAYERS]={0};
	for(int k=0;k<m->n;k++)
	{
		int pnum=r.U8(),mask=r.U8();
		if(!r.Ok()||pnum>=SNAPMAXPLAYERS||seen[pnum]||!mask) return 0;
		seen[pnum]=1;
		SnapPlayer &p=out->p[pnum];
		if(mask==SNAPF_GONE)
		{
			if(!p.in) return 0;
			memset(&p,0,sizeof(p));
			continue;
		}
		if(mask&~SNAPF_ALL||(!p.in&&mask!=SNAPF_ALL)) return 0;
		SyncSnapFields(r,p,mask);
		p.in=1;
	}
	return r.Ok();
}

//a player's state in the game's units, what goes into a SnapPlayer and comes out
struct SnapState
{
	float pos[3];            //x, z: in 0..SNAPWORLD once through the wire
	float vel[3];            //units per 40 ms step
	float yaw,pitch;         //radians: fwd is (sin yaw, 0, cos yaw), tilt (cos pitch, sin pitch, 0)
	float roll;
	unsigned char keys,weapon,playstate,flags;
};

//v rounded into lo..hi (not a number: 0)
inline long SnapRound(double v,double lo,double hi)
{
	if(!(v>=lo)) v=v<lo?lo:0;   //below, or NaN
	if(v>hi) v=hi;
	return lrint(v);
}
//v rounded, modulo 65536 (the world, a turn)
inline uint16_t SnapWrap(double v)
{
	if(!(fabs(v)<1e9)) return 0;
	v=fmod(v,65536.);
	if(v<0) v+=65536;
	return (uint16_t)((unsigned long)lrint(v)&0xFFFF);
}
#define SNAPYAWUNIT (6.28318530718f/65536)   //as MoveCmd::yaw
#define SNAPPITCHUNIT (1/10000.f)

inline void SnapPack(const SnapState &s,SnapPlayer *p)
{
	memset(p,0,sizeof(*p));
	p->in=1;
	//x and z: the world repeats, so only where in it (a negative or a far copy the same)
	p->x=SnapWrap((double)s.pos[0]*128);
	p->z=SnapWrap((double)s.pos[2]*128);
	p->y=(short)SnapRound((double)s.pos[1]*64,-32767,32767);
	for(int k=0;k<3;k++) p->vel[k]=(short)SnapRound((double)s.vel[k]*1024,-32767,32767);
	p->yaw=SnapWrap((double)s.yaw/SNAPYAWUNIT);
	p->pitch=(short)SnapRound((double)s.pitch/SNAPPITCHUNIT,-32767,32767);
	p->roll=(signed char)SnapRound(s.roll,-127,127);
	p->keys=s.keys;
	p->weapon=s.weapon;
	p->playstate=s.playstate;
	p->flags=s.flags;
}

inline void SnapUnpack(const SnapPlayer &p,SnapState *s)
{
	s->pos[0]=p.x/128.f;
	s->pos[1]=p.y/64.f;
	s->pos[2]=p.z/128.f;
	for(int k=0;k<3;k++) s->vel[k]=p.vel[k]/1024.f;
	s->yaw=p.yaw*SNAPYAWUNIT;
	s->pitch=p.pitch*SNAPPITCHUNIT;
	s->roll=p.roll;
	s->keys=p.keys;
	s->weapon=p.weapon;
	s->playstate=p.playstate;
	s->flags=p.flags;
}

//the last SNAPRING snapshots by tick: what the server sent a client (the bases of
//its deltas), what a client received (the bases, and what it draws the others from)
#define SNAPRING 32
struct SnapRing
{
	uint32_t tick[SNAPRING];
	unsigned char used[SNAPRING];
	SnapPlayers s[SNAPRING];
};

inline const SnapPlayers *SnapFind(const SnapRing *r,uint32_t tick)
{
	for(int k=0;k<SNAPRING;k++)
		if(r->used[k]&&r->tick[k]==tick) return &r->s[k];
	return 0;
}

//keeps s as the snapshot of tick, in place of the oldest when full (not if it is
//older than all of them)
inline void SnapStore(SnapRing *r,uint32_t tick,const SnapPlayers &s)
{
	int slot=-1;
	for(int k=0;k<SNAPRING&&slot<0;k++)
		if(r->used[k]&&r->tick[k]==tick) slot=k;
	for(int k=0;k<SNAPRING&&slot<0;k++)
		if(!r->used[k]) slot=k;
	if(slot<0)
	{
		slot=0;
		for(int k=1;k<SNAPRING;k++)
			if(CmdNewer(r->tick[slot],r->tick[k])) slot=k;
		if(CmdNewer(r->tick[slot],tick)) return;
	}
	r->used[slot]=1;
	r->tick[slot]=tick;
	r->s[slot]=s;
}

//what SnapSample found
#define SNAPSAMPLE_NONE 0     //no data at that time: not drawn
#define SNAPSAMPLE_LERP 1     //between two snapshots
#define SNAPSAMPLE_HOLD 2     //the last one it is in (it leaves, or teleports after)
#define SNAPSAMPLE_EXTRAP 3   //past the newest, with its velocity (SNAPEXTRAP at most)

//player pnum at the time tick + frac (frac 0..1): between the snapshots around it
//(position, view and roll; the rest from the older one), past the newest with its
//velocity for SNAPEXTRAP ticks at most. x and z can leave 0..SNAPWORLD (the world
//repeats); the game brings them near.
inline int SnapSample(const SnapRing *r,int pnum,uint32_t tick,float frac,SnapState *out)
{
	int a=-1,b=-1;
	float ta=0,tb=0;   //ticks from tick+frac (a <= 0 < b)
	for(int k=0;k<SNAPRING;k++)
	{
		if(!r->used[k]) continue;
		float d=(float)(int32_t)(r->tick[k]-tick)-frac;
		if(d<=0&&(a<0||d>ta)) { a=k; ta=d; }
		if(d>0&&(b<0||d<tb)) { b=k; tb=d; }
	}
	if(a<0||pnum<0||pnum>=SNAPMAXPLAYERS) return SNAPSAMPLE_NONE;
	const SnapPlayer &pa=r->s[a].p[pnum];
	if(!pa.in) return SNAPSAMPLE_NONE;
	SnapUnpack(pa,out);
	if(b>=0&&r->s[b].p[pnum].in)
	{
		const SnapPlayer &pb=r->s[b].p[pnum];
		float dx=(short)(uint16_t)(pb.x-pa.x)/128.f,dz=(short)(uint16_t)(pb.z-pa.z)/128.f;
		float dy=(pb.y-pa.y)/64.f;
		//a respawn, a teleport, or a death in between: held, then the next
		if(sqrtf(dx*dx+dy*dy+dz*dz)>SNAPTELEPORT||pa.playstate!=pb.playstate) return SNAPSAMPLE_HOLD;
		float f=-ta/(tb-ta);
		out->pos[0]+=dx*f;
		out->pos[1]+=dy*f;
		out->pos[2]+=dz*f;
		for(int k=0;k<3;k++) out->vel[k]+=((pb.vel[k]-pa.vel[k])/1024.f)*f;
		out->yaw+=(short)(uint16_t)(pb.yaw-pa.yaw)*SNAPYAWUNIT*f;
		out->pitch+=(pb.pitch-pa.pitch)*SNAPPITCHUNIT*f;
		out->roll+=(pb.roll-pa.roll)*f;
		return SNAPSAMPLE_LERP;
	}
	if(b>=0) return SNAPSAMPLE_HOLD;   //gone in the next one
	float dt=-ta;
	if(dt>SNAPEXTRAP) dt=SNAPEXTRAP;
	for(int k=0;k<3;k++) out->pos[k]+=out->vel[k]*dt*SNAPSTEPSPERTICK;
	return SNAPSAMPLE_EXTRAP;
}

//---- phase 5: shots at the tick seen, predicted grenades, timestamped events ----

//FIREGUN and THROWSMOKE from a client: the FIREGUN bytes (FIREGUNSIZE, netmsg.h),
//then FIRETICKSIZE more (the server refuses a shot without them):
//  uint32 viewtick, uint8 viewfrac  the moment it drew the others when it shot (the
//                                   server's tick and 1/256: phase 4's render time)
//  uint32 cmd                       its last command applied: it shot from there
//  uint16 objid                     its number for the grenade or smoke grenade it
//                                   drew at once (0: a bullet, a reload)
#define FIRETICKAT 27        //FIREGUNSIZE
#define FIRETICKSIZE 11
struct FireTick
{
	uint32_t viewtick;   //0: it doesn't draw the others from snapshots (yet)
	unsigned char viewfrac;
	uint32_t cmd;
	uint16_t objid;
};
template<class IO> inline void SyncFireTick(IO &io,FireTick &t)
{
	io.u32(t.viewtick);
	io.u8(t.viewfrac);
	io.u32(t.cmd);
	io.u16(t.objid);
}
//the extension alone: at msg+FIRETICKAT, len-FIRETICKAT bytes (0: there is none)
inline int WriteFireTick(void *buf,int size,FireTick *t)
{
	ByteWriter w(buf,size);
	SyncFireTick(w,*t);
	return w.Ok()?w.Size():0;
}
inline int ReadFireTick(const void *buf,int len,FireTick *t)
{
	ByteReader r(buf,len);
	SyncFireTick(r,*t);
	return r.Ok();
}
//FIREGUN relayed by the server (a shot of someone else): the FIREGUN bytes, then
//uint32 tick, the server's tick of the shot
#define FIRERELAYSIZE 4
inline int ReadFireRelayTick(const void *msg,int len,uint32_t *tick)
{
	if(len<FIRETICKAT+FIRERELAYSIZE) return 0;
	*tick=WireGetU32((const unsigned char *)msg+FIRETICKAT);
	return 1;
}

//TICKEVENT (server to client, reliable): something that happened at a tick of the
//server, which the client applies when the moment it draws the others reaches it
//(the victim falls where it is seen; at once if it is about our own player):
//  uint8 type (23), uint32 tick, uint8 kind, then by kind:
//  TE_KILLED   uint8 killed, uint8 killer, uint8 haspos, float pos[3] if haspos
//              (KILLED, KILLEDPOSITION: where it died)
//  TE_PAIN     uint8 player, uint16 sound (PAIN: a hit, a death cry)
//  TE_GRENADE  uint8 owner, uint16 objid, float pos[3], float vel[3], uint16 fuse ms
//              (the server's grenade; its thrower's predicted copy has that objid)
//  TE_EXPLODE  uint8 owner, uint16 objid, float pos[3] (where the server's went off)
//  TE_SMOKE    uint8 owner, uint16 objid, uint8 slot (sobject: the server's smoke
//              grenade, to its thrower: it takes the place of the predicted one)
#define TICKEVENT 23
#define TE_KILLED 1
#define TE_PAIN 2
#define TE_GRENADE 3
#define TE_EXPLODE 4
#define TE_SMOKE 5
struct TickEvent
{
	char type;
	uint32_t tick;
	unsigned char kind;
	unsigned char player;   //killed, hurt, owner
	unsigned char other;    //TE_KILLED the killer, TE_SMOKE the slot
	unsigned char haspos;   //TE_KILLED: pos is there
	uint16_t num;           //TE_PAIN the sound; the objid
	uint16_t ms;            //TE_GRENADE: ms before it goes off
	float pos[3],vel[3];
};
template<class IO> inline void SyncTickEvent(IO &io,TickEvent &e)
{
	io.i8(e.type);
	io.u32(e.tick);
	io.u8(e.kind);
	switch(e.kind)
	{
	case TE_KILLED:
		io.u8(e.player);
		io.u8(e.other);
		io.u8(e.haspos);
		if(e.haspos) for(int k=0;k<3;k++) io.f32(e.pos[k]);
		break;
	case TE_PAIN:
		io.u8(e.player);
		io.u16(e.num);
		break;
	case TE_GRENADE:
		io.u8(e.player);
		io.u16(e.num);
		for(int k=0;k<3;k++) io.f32(e.pos[k]);
		for(int k=0;k<3;k++) io.f32(e.vel[k]);
		io.u16(e.ms);
		break;
	case TE_EXPLODE:
		io.u8(e.player);
		io.u16(e.num);
		for(int k=0;k<3;k++) io.f32(e.pos[k]);
		break;
	case TE_SMOKE:
		io.u8(e.player);
		io.u16(e.num);
		io.u8(e.other);
		break;
	}
}
#define TICKEVENTMAX (1+4+1+1+2+12+12+2)
inline int WriteTickEvent(void *buf,int size,TickEvent *e)
{
	if(e->kind<TE_KILLED||e->kind>TE_SMOKE) return 0;
	ByteWriter w(buf,size);
	SyncTickEvent(w,*e);
	return w.Ok()?w.Size():0;
}
//0 for an unknown kind, or cut short; haspos is 0 or 1. The players, slot and
//floats are the caller's to check.
inline int ReadTickEvent(const void *buf,int len,TickEvent *e)
{
	memset(e,0,sizeof(*e));
	ByteReader r(buf,len);
	SyncTickEvent(r,*e);
	return r.Ok()&&e->kind>=TE_KILLED&&e->kind<=TE_SMOKE&&e->haspos<=1;
}

#endif
