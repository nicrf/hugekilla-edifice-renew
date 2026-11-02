//The messages of the server-authoritative movement (docs/M16.md, phases 2 and 3):
//the client's numbered input commands (INPUTCMD) and the server's exact state of its
//own player (SNAPSHOT, phase 2 form: no other player yet). Only between a client
//with HELLOCAP_INPUTS and a server that said it moves it (NETSYNCMODE). Apart from
//netmsg.h, on wire.h alone, so tests/wire_test.cpp and the movement code use them
//without the game.

#ifndef NETSYNCH
#define NETSYNCH

#include "wire.h"

#define INPUTCMD 21   //client to server, unreliable, every tick
#define SNAPSHOT 22   //server to client, unreliable, 30 Hz
//SIMPLE message: msg = the HELLOCAP_* sync bits the server applies to this client
//(HELLOCAP_INPUTS: it moves the client's player from its commands), msg2 = the
//server's tick. At the join and when sv_authmove changes; only to clients with
//HELLOCAP_INPUTS.
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
//losses), uint32 firstcmd (its number: the client's tick), uint32 acksnap (phase 4,
//0 before), n commands
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
//its player follows, SYNCSTATESIZE bytes), uint8 n (other players, phase 4: 0 here)
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
//the header and the state of our player; what follows (other players, phase 4) is
//not read yet
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

#endif
