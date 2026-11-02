//A player's move for one step of the simulation (docs/M16.md, phase 0): the physics
//Play() did inline for every player, out on its own, so the server can run it from a
//client's inputs and the client predict with the same steps (phases 2 and 3). It
//reads and writes the player only; the land comes through MoveWorld, the sounds and
//particles go out as MoveEvents (playing.cpp plays them). Tested by
//tests/movement_test.cpp.

#ifndef MOVEMENTH
#define MOVEMENTH

#include "playing.h"   //Player, ServerVars

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

#endif
