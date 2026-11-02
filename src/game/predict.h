//The client's prediction of its own player (docs/M16.md, phase 3): each tick's
//command is applied at once (ApplyMoveCmd) and kept, with the state it led to, in
//a ring of 128 (2 s); when the server's state after one of them comes back, it is
//compared with what was predicted for it and, if they differ, the state is the
//server's from there and the commands after it are played again. multiplay.cpp
//sends and receives; playing.cpp smooths what is drawn. Tested by
//tests/movement_test.cpp.

#ifndef PREDICTH
#define PREDICTH

#include "movement.h"

#define PREDICTRING 128
#define PREDICTPOSERR .01f    //units: a smaller gap with the server is left alone
#define PREDICTVELERR .001f   //units per step
#define PREDICTSNAP 3.0f      //units: a correction further than this isn't smoothed

struct PredictEntry
{
	unsigned int num;     //the command's number (the client's tick)
	MoveCmd cmd;
	MoveState after;      //the state it led to
	float speedpenalty;   //what the move had (GetSOSpeedPenalty)
	char playstate;
	char valid;
};

struct Prediction
{
	PredictEntry ring[PREDICTRING];
	unsigned int latest;   //newest command predicted (any: there is one)
	int any;
	unsigned int acked;    //newest command the server's state was compared for
	int anyack;
	float speedpenalty;    //the server's (with its state, anyack): what the next
	                       //commands are predicted with, not what we think we carry
};

//what a state from the server did
#define PREDICT_STALE 0       //older than one already compared, or no longer kept
#define PREDICT_OK 1          //as predicted
#define PREDICT_CORRECTED 2   //the server's, and the commands after it again
struct PredictResult
{
	int kind;
	float poserr,velerr;  //between the prediction and the server
	int replayed;         //commands played again
	Vector shift;         //where the player is now minus where it was (corrected)
};

void PredictReset(Prediction *pr);
//one tick: command num applied to p (the local player) and kept; speedpenalty: ours,
//until the server has said its own
void PredictTick(Prediction *pr,Player *p,unsigned int num,const MoveCmd &c,float speedpenalty,
	float sf,const ServerVars &sv,const MoveWorld &w,MoveEvents *ev);
//the server's state after command num (legframe isn't sent: the predicted one) and
//its speed penalty, which the commands after it are played again with if they had
//another (it took or dropped something before we knew)
PredictResult PredictReconcile(Prediction *pr,Player *p,unsigned int num,const MoveState &server,
	float speedpenalty,float sf,const ServerVars &sv,const MoveWorld &w);
//the entry of command num, 0 if it isn't kept
const PredictEntry *PredictFind(const Prediction *pr,unsigned int num);

#endif
