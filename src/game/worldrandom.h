// Deterministic random numbers for the parts of the world every client builds
// on its own from the LandDefs the server sends (scenery, ground blotches, lily
// pads). rand() gives different sequences with each C library (macOS, Linux,
// Windows), so players on different systems saw different worlds; PCG32 is the
// same everywhere. Changing anything here changes the world: bump
// EDIFICE_PROTOCOL_VERSION (src/net/version.h). tests/worldrandom_test.cpp
// checks the sequence on every platform.

#ifndef WORLDRANDOM_H
#define WORLDRANDOM_H

#include <stdint.h>

struct WorldRandom
{
	uint64_t state;
	uint64_t inc;

	// One independent sequence per (world seed, kind of object, object index):
	// a float difference while placing one object can't shift all the others.
	void Seed(uint32_t seed, uint32_t stream, uint32_t index)
	{
		uint64_t mix = ((uint64_t)seed << 32) ^ ((uint64_t)stream << 24) ^ index;
		// splitmix64, to spread similar seeds apart
		mix += 0x9E3779B97F4A7C15ull;
		mix = (mix ^ (mix >> 30)) * 0xBF58476D1CE4E5B9ull;
		mix = (mix ^ (mix >> 27)) * 0x94D049BB133111EBull;
		mix ^= mix >> 31;
		state = 0;
		inc = (((uint64_t)stream << 1) | 1u);
		Next();
		state += mix;
		Next();
	}

	uint32_t Next()
	{
		uint64_t old = state;
		state = old * 6364136223846793005ull + inc;
		uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
		uint32_t rot = (uint32_t)(old >> 59u);
		return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
	}

	// [0, 1): unlike the game's RND, never exactly 1
	float Float()
	{
		return (float)(Next() >> 8) * (1.0f / 16777216.0f);
	}
};

#endif
