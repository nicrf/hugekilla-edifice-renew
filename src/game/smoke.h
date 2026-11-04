//Smoke grenades (docs/ROADMAP.md, M13.6 10b): the grenade weapon's second mode. A
//thrown smoke grenade is a server object (sobject[], SMOKECLOUD): the server flies
//it, lands it and lets it live SMOKE_LIFETIME seconds; every client that announced
//HELLOCAP_SMOKE (version.h) gets it in UPDATESERVEROBJECT and the snapshot, others
//never see it (an empty slot). The cloud only hides: bullets go through it (the
//server's hit test is unchanged). Drawn by effects.cpp: raymarched lumps in Ultra,
//thick sprites in Classic, both about as opaque.
//
//What follows is pure (floats, no game state): the cloud's size and opacity over
//its life, where its lumps are, a step of its flight, and how the server object
//carries the state on the wire (tests/smoke_test.cpp).

#ifndef SMOKEH
#define SMOKEH

#include <math.h>

#define SMOKECLOUD 5            //server object type (gameplay.h: DOME 1, WATERPUMP 2, FLAG 3)
#define SMOKE_LIFETIME 25.0f    //seconds from landing
#define SMOKE_FADE 4.0f         //its last seconds: it thins out and spreads a little
#define SMOKE_RADIUS 7.0f       //units, fully grown (a block is 2, a player 1.5 tall)
#define SMOKE_GROWTH 1.8f       //seconds: e-folding time of the growth
#define SMOKE_MAXFLIGHT 8.0f    //seconds: a grenade still flying then lands where it is
#define SMOKE_MAXCLOUDS 8       //on the server at once (flying or not)
#define SMOKE_LUMPS 8           //Ultra: lumps of a cloud
#define SMOKE_SPRITES 22        //Classic: sprites of a cloud

//radius of the cloud age seconds after it landed (0 before)
inline float SmokeRadius(float age)
{
	if(age<=0) return 0;
	float r=SMOKE_RADIUS*(1-expf(-age/SMOKE_GROWTH));
	float fade=age-(SMOKE_LIFETIME-SMOKE_FADE);
	if(fade>0) r*=1+.15f*(fade>SMOKE_FADE?1:fade/SMOKE_FADE);
	return r;
}

//its opacity 0..1: quickly thick, then thinning out over its last SMOKE_FADE seconds
inline float SmokeStrength(float age)
{
	if(age<=0||age>=SMOKE_LIFETIME) return 0;
	float in=age/.6f;
	if(in>1) in=1;
	float out=(SMOKE_LIFETIME-age)/SMOKE_FADE;
	if(out>1) out=1;
	return in*out;
}

//a hash of the cloud's slot and a lump: 0..1
inline float SmokeHash(int slot,int k,int salt)
{
	unsigned h=(unsigned)slot*2654435761u^(unsigned)k*2246822519u^(unsigned)salt*3266489917u;
	h^=h>>15;
	h*=2246822519u;
	h^=h>>13;
	return (h&0xffffff)/16777216.0f;
}

//lump or sprite k of the cloud in slot: its offset from where the grenade lies
//(x, y, z) and its radius, for a cloud of radius r, turning slowly with age.
//The same on every client: the cloud looks alike everywhere.
inline void SmokePart(int slot,int k,int n,float r,float age,float out[4])
{
	//spread evenly around, at a random distance and height; the first one in the middle
	float a=(k+SmokeHash(slot,k,1)*.6f)*6.2831853f/n+age*.04f;
	float d=k?(.25f+.35f*SmokeHash(slot,k,2))*r:0;
	out[0]=cosf(a)*d;
	out[2]=sinf(a)*d;
	out[1]=r*(.3f+.3f*SmokeHash(slot,k,3));
	out[3]=r*(.55f+.15f*SmokeHash(slot,k,4));
}

//a step of sf (the game's frame scale) of a grenade in flight, as TickObjects moves
//frag grenades: ground is the land's height under pos after the step. Returns 1
//when it lands (it stops there).
inline int SmokeFly(float pos[3],float vel[3],float sf,float gravity,float ground)
{
	pos[0]+=vel[0]*sf;
	pos[1]+=vel[1]*sf;
	pos[2]+=vel[2]*sf;
	vel[1]-=gravity*sf;
	if(pos[1]<=ground)
	{
		pos[1]=ground;
		vel[0]=vel[1]=vel[2]=0;
		return 1;
	}
	return 0;
}

//the state a server object carries for a smoke grenade (its dir: x phase, 0 flying
//1 a cloud; y age in seconds when sent; z lifetime). On the server the expiry time
//is its timeout; a client sets its own from the age it receives.
inline void SmokeEncode(float dir[3],int landed,float age)
{
	dir[0]=landed?1.0f:0.0f;
	dir[1]=age<0?0:age;
	dir[2]=SMOKE_LIFETIME;
}
inline int SmokeLanded(const float dir[3])
{
	return dir[0]>.5f;
}
//ms the received cloud still has to live (0: over, or nonsense from the wire)
inline int SmokeRemainingMs(const float dir[3])
{
	float age=dir[1];
	if(!(age>=0)||age>=SMOKE_LIFETIME) return 0;   //NaN too
	return (int)((SMOKE_LIFETIME-age)*1000);
}

//what a server object becomes for a peer: a client without HELLOCAP_SMOKE never
//gets a smoke grenade, the slot is empty for it (an older build would index its
//models by the type and could let its player pick it up)
inline int SmokeHiddenFrom(int type,int peerunderstands)
{
	return type==SMOKECLOUD&&!peerunderstands;
}

//---- the game's side (smoke.cpp) ----

#include "vector.h"
struct Player;

//the local player's grenade mode (the grenade key again switches it); off when the
//server can't have smoke grenades
int SmokeModeOn();
void SmokeToggleMode();
void SmokeSetMode(int on);   //quietly (the weapon wheel: the smoke grenade is a step of its own)
//client: whether the server takes smoke grenades now (SMOKEMODE); the host decides itself
void SmokeSetAllowed(int allowed);
int SmokeAllowed();
//the local player throws one (FireGun, grenade in smoke mode): 1 if it went
int ThrowSmokeGrenade(Player *cur);
//the magazine and reserves of cur's weapon: the smoke grenades' own when the local
//player holds the grenade in smoke mode
int *SmokeAmmoInGun(Player *cur);
int *SmokeAmmoReserves(Player *cur);
//server: player pnum threw one from pos along dir (already checked); its slot in
//sobject, -1 if there is no room
int ServerThrowSmoke(int pnum,const Vector &pos,const Vector &dir);
//client (M16 phase 5): the server's smoke grenade in slot takes the place of ours
//numbered objid (TE_SMOKE), where ours is now
void SmokeTakeOver(int objid,int slot);
//flights (the server lands them, clients follow), every game step
void TickSmoke(float sf);
//client: sobject[i] came from the server, sets the time it has left
void SmokeReceived(int i);
//seconds since sobject[i] landed as a cloud; -1 if it isn't one (yet)
float SmokeCloudAge(int i);

#endif
