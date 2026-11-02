//A player's move for one step of the simulation (docs/M16.md, phase 0): the physics
//Play() did inline for every player, out on its own, so the server can run it from a
//client's inputs and the client predict with the same steps (phases 2 and 3). It
//reads and writes the player only; the land comes through MoveWorld, the sounds and
//particles go out as MoveEvents (playing.cpp plays them). Tested by
//tests/movement_test.cpp.

#ifndef MOVEMENTH
#define MOVEMENTH

#include "playing.h"   //Player, ServerVars
#include "netsync.h"   //MoveCmd

//what is not the player's own state
struct MoveInput
{
	int local;           //the local player: its glider folds when the key is released
	                     //(the others' gliding comes from the network)
	int pad;             //walks with a stick (SetPadMove), how far it is pushed:
	float padfwd,padstrafe;
	float speedpenalty;  //GetSOSpeedPenalty: carrying a flag, a water pump...
	MoveInput():local(0),pad(0),padfwd(0),padstrafe(0),speedpenalty(0) {}
};

//the land: GetHeight (the ground or a block under a point), PropsPushPlayer (rocks
//and trees, props.h)
struct MoveWorld
{
	float (*height)(Vector pos);
	int (*pushprops)(Vector &pos,Vector &vel,float radius,float feet,float head);
};

//what happened that can be heard or seen, at the place it happened
struct MoveEvents
{
	int thrust;              //1: the jetpack pushed (thrustpos, along thrustdir), 0: it
	Vector thrustpos;        //didn't, -1: neither (gliding: the jetpack is left as it was)
	Vector thrustdir;
	int landed;              //on the ground falling fast
	Vector landpos;
	int walked;              //on the ground with a walk key held, alive
	Vector walkpos;
	int splash;              //goes into the water
	Vector splashpos;
	MoveEvents():thrust(-1),landed(0),walked(0),splash(0) {}
};

//the view direction from fwd and tilt, the aim's wobble; then the game may turn
//the player (a bot) before it moves
void AimPlayer(Player *p);
//one step of sf (40 ms of the original game per unit) for a player in the game
//(playstate 10, or 11 waiting to respawn: it falls but doesn't walk, jump or fly)
void MovePlayer(Player *p,const MoveInput &in,float sf,const ServerVars &sv,const MoveWorld &w,MoveEvents *ev);

//---- input commands (docs/M16.md, phases 2 and 3; the wire format is netsync.h) ----

//what of a player a move changes: what the client predicts and the server sends back
struct MoveState
{
	Vector pos,vel;
	float thrustfuel,roll,runtime;
	int legframe;
	char hanggliding,jump;
};
void GetMoveState(const Player *p,MoveState *s);
void SetMoveState(Player *p,const MoveState &s);

//the command of the local player's keys, view and stick (pad: -1..1, used if
//active) for one tick; num: its number (the tick), for the 16/17 ms it lasts
MoveCmd MakeMoveCmd(const Player *p,int padactive,float padfwd,float padstrafe,unsigned int num);
//the view a command carries: fwd, tilt, and the direction the move uses (without
//the aim's wobble, so that the server and the client agree)
void MoveCmdView(const MoveCmd &c,Vector *fwd,Vector *tilt,Vector *dir);
//x and z into LANDGRIDSIZE..2*LANDGRIDSIZE, as Play() keeps the local player
void WrapPlayerPos(Vector *pos);
//one tick of a command for player p (playstate as it is): its keys, the stick, its
//view (kept on the player if keepview is 0, the server; else only for the move,
//the client's own view stays the mouse's), then MovePlayer. Deterministic: the same
//commands from the same state give the same state, to the bit.
void ApplyMoveCmd(Player *p,const MoveCmd &c,float speedpenalty,float sf,const ServerVars &sv,
	const MoveWorld &w,MoveEvents *ev,int keepview);

#endif
