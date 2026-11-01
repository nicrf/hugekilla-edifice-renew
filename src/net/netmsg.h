//wire format of each network message (roadmap M6, primitives in wire.h), apart
//from multiplay.cpp so that the parsers can be tested and fuzzed without the
//game (tests/fuzz/). Every Read* only decodes and bounds what it reads: the
//caller still validates indices (players, objects) against the game state.

#ifndef NETMSGH
#define NETMSGH

#include "wire.h"
#include "landwire.h"
#include "multiplaystruct.h"
#include "multiplay.h"   //MAXPLAYERS

//No struct is copied onto or off the wire: Sync<msg> lists the fields once, in
//order, and WIREPAIR makes Write<msg> (returns the size, 0 if it doesn't fit)
//and Read<msg> (returns 0 for a short, malformed message) from it.
//Little-endian, floats as IEEE 754 bits, no padding; the type byte comes first.

template<class IO> inline void SyncVec(IO &io,Vector &v)
{
	io.f32(v.x);
	io.f32(v.y);
	io.f32(v.z);
}

#define WIREPAIR(name,T) \
inline int Write##name(void *buf,int size,T *s) { ByteWriter w(buf,size); Sync##name(w,*s); return w.Ok()?w.Size():0; } \
inline int Read##name(const void *buf,int len,T *s) { ByteReader r(buf,len); Sync##name(r,*s); return r.Ok(); }

//PLAYERPOSITION, 16 bytes
template<class IO> inline void SyncPosition(IO &io,PlayerPosition &p)
{
	io.i8(p.type);
	io.i8(p.pnum);
	io.i8(p.weapon);
	io.i8(p.keys);
	io.i8(p.state);
	io.u8(p.fwd);
	io.u8(p.tilt);
	io.u16(p.posx);
	io.u16(p.posy);
	io.u16(p.posz);
	io.i8(p.velx);
	io.i8(p.vely);
	io.i8(p.velz);
}
WIREPAIR(Position,PlayerPosition)
#define POSITIONSIZE 16
//from a client with HELLOCAP_POSSEQ (M11 phase 3b): + uint16 sequence number,
//one more for each position it sends. Older servers read the first 16 bytes only.
#define POSITIONSEQSIZE (POSITIONSIZE+2)
//the sequence number of a position, -1 if it has none
inline int ReadPositionSeq(const void *buf,int len)
{
	if(len<POSITIONSEQSIZE) return -1;
	return WireGetU16((const unsigned char *)buf+POSITIONSIZE);
}

//FIREGUN, 27 bytes
template<class IO> inline void SyncFireGun(IO &io,FireGunDirection &f)
{
	io.i8(f.type);
	io.i8(f.pnum);
	io.i8(f.reload);
	SyncVec(io,f.dir);
	SyncVec(io,f.pos);
}
WIREPAIR(FireGun,FireGunDirection)
#define FIREGUNSIZE 27

//KILLEDPOSITION, 15 bytes
template<class IO> inline void SyncKilledPosition(IO &io,KilledPosition &k)
{
	io.i8(k.type);
	io.i8(k.killed);
	io.i8(k.killer);
	SyncVec(io,k.pos);
}
WIREPAIR(KilledPosition,KilledPosition)
#define KILLEDPOSITIONSIZE 15

//a server object, 43 bytes; timeout stays on the server
template<class IO> inline void SyncServerObject(IO &io,ServerObject &o)
{
	io.i8(o.type);
	SyncVec(io,o.pos);
	SyncVec(io,o.dir);
	SyncVec(io,o.vel);
	io.i8(o.team);
	io.i8(o.attached);
	io.f32(o.speedpenalty);
}
#define SERVEROBJECTSIZE 43

//UPDATESERVEROBJECT: type, int32 num, object
template<class IO> inline void SyncUpdateServerObject(IO &io,UpdateServerObject &u)
{
	io.i8(u.type);
	io.i32(u.num);
	SyncServerObject(io,u.updated);
}
WIREPAIR(UpdateServerObject,UpdateServerObject)
#define UPDATESERVEROBJECTSIZE (5+SERVEROBJECTSIZE)

//SERVEROBJECTSNAPSHOT: type, then every server object
struct ServerObjectSnapshot
{
	char type;
	ServerObject obj[MAXSERVEROBJS];
};
template<class IO> inline void SyncSnapshot(IO &io,ServerObjectSnapshot &s)
{
	io.i8(s.type);
	for(int i=0;i<MAXSERVEROBJS;i++) SyncServerObject(io,s.obj[i]);
}
WIREPAIR(Snapshot,ServerObjectSnapshot)
#define SNAPSHOTSIZE (1+MAXSERVEROBJS*SERVEROBJECTSIZE)

//SERVERVARS: type, then the fields of ServerVars in declaration order
struct ServerVarsMessage
{
	char type;
	ServerVars v;
};
template<class IO> inline void SyncServerVars(IO &io,ServerVarsMessage &m)
{
	ServerVars &v=m.v;
	io.i8(m.type);
	io.f32(v.walkaccelerate);
	io.f32(v.strafeaccelerate);
	io.f32(v.resistance);
	io.f32(v.stopresistance);
	io.f32(v.gravity);
	io.i32(v.chopperthrust);
	io.i32(v.radarenable);
	io.i32(v.gattimeout);
	io.i32(v.m16timeout);
	io.i32(v.steyrtimeout);
	io.i32(v.umptimeout);
	io.i32(v.grenadetimeout);
	io.i32(v.berettatimeout);
	io.i32(v.sr1000timeout);
	io.i32(v.grenadeexplodetime);
	io.i32(v.gatammohold);
	io.i32(v.m16ammohold);
	io.i32(v.steyrammohold);
	io.i32(v.umpammohold);
	io.i32(v.grenadeammohold);
	io.i32(v.berettaammohold);
	io.i32(v.sr1000ammohold);
	io.i32(v.gatreloadtime);
	io.i32(v.m16reloadtime);
	io.i32(v.steyrreloadtime);
	io.i32(v.umpreloadtime);
	io.i32(v.grenadereloadtime);
	io.i32(v.berettareloadtime);
	io.i32(v.sr1000reloadtime);
	io.f32(v.gatspeed);
	io.f32(v.m16speed);
	io.f32(v.steyrspeed);
	io.f32(v.umpspeed);
	io.f32(v.grenadespeed);
	io.f32(v.berettaspeed);
	io.f32(v.sr1000speed);
	io.f32(v.highbulletdamage);
	io.f32(v.lowbulletdamage);
	io.f32(v.grenadedamage);
	io.f32(v.scale);
	io.f32(v.thrustrechargerate);
	io.f32(v.thrustuse);
	io.i32(v.capturelimit);
	io.i32(v.timelimit);
	io.i32(v.gamemode);
	io.i32(v.positionresendtime);
}
WIREPAIR(ServerVars,ServerVarsMessage)
#define SERVERVARSSIZE (1+46*4)
static_assert(sizeof(ServerVars)==46*4,"ServerVars changed: update SyncServerVars and bump the protocol");

//REMOTEGAMEDATA: type, int32 gamestate
struct RemoteGameDataMessage
{
	char type;
	RemoteGameData d;
};
template<class IO> inline void SyncRGD(IO &io,RemoteGameDataMessage &m)
{
	io.i8(m.type);
	io.i32(m.d.gamestate);
}
WIREPAIR(RGD,RemoteGameDataMessage)
#define RGDSIZE 5

//LANDINFO: type, LandDefs (every deformation), SkySettings (landwire.h)
template<class IO> inline void SyncLandInfo(IO &io,LandInfo &l)
{
	io.i8(l.type);
	SyncLandDefs(io,l.ldfs);
	SyncSkySettings(io,l.skysets);
}
WIREPAIR(LandInfo,LandInfo)
#define LANDINFOSIZE (1+LANDDEFSSIZE+SKYSETTINGSSIZE)

//REQUESTENTER and INFO: type, uint8 n, name[n], NUL, version trailer (version.h).
//Copies the name (at most namesize-1 bytes, up to a NUL) and returns the offset
//of the trailer.
inline int ReadHelloName(const char *msg,int msglen,char *name,int namesize)
{
	int n=msglen>=2?(unsigned char)msg[1]:0;
	if(n>namesize-1) n=namesize-1;
	int i;
	for(i=0;i<n&&2+i<msglen&&msg[2+i];i++) name[i]=msg[2+i];
	name[i]=0;
	return 2+n+1;
}

//PLAYERNAMES: uint8 count, then per player: uint8 number, int32 kills,
//int32 deaths, int8 team, uint8 name length (<=31), name
#define PLAYERNAMESIZE (11+31)
struct PlayerNameEntry
{
	int num;          //player number, not checked (see ValidPlayer)
	int kills,deaths;
	char team;
	char name[32];
};
//decodes up to max entries (and never more than MAXPLAYERS) into out and returns
//how many; stops at the first truncated or malformed entry, like the original loop
inline int ReadPlayerNames(const void *msg,int msglen,PlayerNameEntry *out,int max)
{
	ByteReader r(msg,msglen);
	r.U8();   //type
	int numnames=r.U8();
	if(!r.Ok()) return 0;
	if(numnames>MAXPLAYERS) numnames=MAXPLAYERS;
	if(numnames>max) numnames=max;
	int n;
	for(n=0;n<numnames;n++)
	{
		PlayerNameEntry &e=out[n];
		e.num=r.U8();
		e.kills=r.I32();
		e.deaths=r.I32();
		e.team=(char)r.U8();
		int namelen=r.U8();
		if(!r.Ok()||namelen>31||namelen>r.Left()) break;
		r.Bytes(e.name,namelen);
		e.name[namelen]=0;
	}
	return n;
}

//CHATMESSAGE: type, int8 dest (CMSG_*), uint8 sender, uint16 length, text
#define CHATHEADERSIZE 5
#define CHATMAXTEXT 255
struct ChatMessage
{
	char dest;
	int sender;
	int len;                  //<=CHATMAXTEXT and <= the bytes received
	char text[CHATMAXTEXT+1]; //NUL-terminated
};
//0 if the header is truncated; a text longer than what was received is cut
inline int ReadChat(const void *msg,int msglen,ChatMessage *c)
{
	ByteReader r(msg,msglen);
	r.U8();   //type
	c->dest=(char)r.U8();
	c->sender=r.U8();
	int len=r.U16();
	if(!r.Ok()) return 0;
	if(len>CHATMAXTEXT) len=CHATMAXTEXT;
	if(len>r.Left()) len=r.Left();
	r.Bytes(c->text,len);
	c->text[len]=0;
	c->len=len;
	return 1;
}

//BLOCKS: type, uint32 count, then count blocks of 9 bytes: int8 x y z type
//team top side bottom, uint8 del (nonzero: delete the block at x,y,z)
#define BLOCKSHEADERSIZE 5
#define BLOCKEDITSIZE 9
struct BlockEdit
{
	char x,y,z,type,team,top,side,bottom,del;
};
//number of blocks in the message, bounded by the bytes received (0 if truncated)
inline int ReadBlocksCount(const void *msg,int msglen)
{
	if(msglen<BLOCKSHEADERSIZE) return 0;
	DWORD n=WireGetU32((const char *)msg+1);
	DWORD fit=(DWORD)(msglen-BLOCKSHEADERSIZE)/BLOCKEDITSIZE;
	return (int)(n<fit?n:fit);
}
//block i (< ReadBlocksCount) of the message
inline void ReadBlockEdit(const void *msg,int i,BlockEdit *b)
{
	const char *p=(const char *)msg+BLOCKSHEADERSIZE+i*BLOCKEDITSIZE;
	b->x=p[0];
	b->y=p[1];
	b->z=p[2];
	b->type=p[3];
	b->team=p[4];
	b->top=p[5];
	b->side=p[6];
	b->bottom=p[7];
	b->del=p[8];
}
//what AddBlock and the renderer can take (a deletion only needs its position)
inline int ValidBlockEdit(const BlockEdit *b)
{
	return b->del||ValidBlock(b->type,b->top,b->side,b->bottom);
}

#endif
