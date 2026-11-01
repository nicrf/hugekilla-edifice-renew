//Visual effects of M13.5 (explosions...), client side only: the game triggers them
//where something happens (playing.cpp), nothing goes over the network. They live
//in their own pool, apart from the game's particle[] (rain, snow, jetpack), move
//with the real frame time and draw as billboards through renderer.h, so both
//backends show them. Each one is switched in Options (fxprefs, redrawing.h).

#ifndef EFFECTSH
#define EFFECTSH

#include "vector.h"

//a grenade going off at pos: flash, fireball, sparks, debris, light on the ground,
//and the smoke that stays (fxprefs.smoke; grenades in flight leave a trail too)
void FxExplosion(const Vector &pos);

//procedural textures of the effects (LoadTextures)
void FxLoadTextures();
//moves and draws every effect: Draw (redrawing.cpp), with the scene's camera, after
//the water
void DrawEffects();

#endif
