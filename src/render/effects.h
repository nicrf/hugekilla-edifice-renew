//Visual effects of M13.5 (explosions...), client side only: the game triggers them
//where something happens (playing.cpp), nothing goes over the network. They live
//in their own pool, apart from the game's particle[] (rain, snow, jetpack), move
//with the real frame time and draw as billboards through renderer.h, so both
//backends show them. Each one is switched in Options (fxprefs, redrawing.h).

#ifndef EFFECTSH
#define EFFECTSH

#include "vector.h"

//a grenade going off at pos: flash, fireball, sparks, debris, light on the ground,
//and the smoke that stays (fxprefs.smoke; grenades in flight leave a trail too);
//under water (fxprefs.impacts): bubbles and the water thrown up instead
void FxExplosion(const Vector &pos);

//the rest are fxprefs.impacts, which the caller checks
//something crossing the water's surface (a bullet 1, a grenade 2, an explosion
//under water up to 5): ripples, drops, spray
void FxWaterSplash(const Vector &at,float strength);
//air rising under water from around pos (explosions, impacts on the lake bed)
void FxBubbles(const Vector &pos,int count,float spread);
//a bullet's way under water, one tick's segment: a bubble now and then
void FxBulletWake(const Vector &from,const Vector &to);
//a bullet hitting the land (or a block, onblock 1, or the lake bed), going along
//dir (normalized): dust, grains, sparks off a block
void FxBulletImpact(const Vector &hit,const Vector &dir,int onblock);

//drawn at once (DrawGunExplosions, fxprefs.muzzleflash): the glow of a shot around
//the muzzle sprite, bright 0..1, turned by rot (radians) so it flickers
void FxMuzzleGlow(const Vector &pos,float size,float bright,float rot);

//procedural textures of the effects (LoadTextures)
void FxLoadTextures();
//moves and draws the effects: Draw (redrawing.cpp), with the scene's camera; layer
//0 before the water (what is under it), 1 after
void DrawEffects(int layer);

#endif
