// Golden values of WorldRandom (src/game/worldrandom.h). The world every client
// builds depends on this exact sequence, so it must be identical on macOS,
// Linux and Windows: CI runs this test on all three. If you change the
// generator on purpose, regenerate the values and bump EDIFICE_PROTOCOL_VERSION.

#include <stdio.h>
#include "worldrandom.h"

struct Golden
{
	uint32_t seed, stream, index;
	uint32_t next[4];
	float first;
};

static const Golden golden[] =
{
	{0u,1u,0u,{0x1d15aa4bu,0xd86e0c37u,0x501a0e46u,0x4d63dfc5u},0.113611817f},
	{12345u,1u,0u,{0x373de535u,0x0677d8fcu,0xa042fffeu,0x75474b7bu},0.215788186f},
	{12345u,1u,1u,{0xbf5a9212u,0x629778cdu,0x5aa30397u,0xb440bda4u},0.747475743f},
	{12345u,2u,7u,{0x4d53bf87u,0x685a2fd8u,0x11f63057u,0x961bf7b3u},0.302059114f},
	{4000000000u,3u,199u,{0xa7cb3241u,0x28b43d41u,0xdb655dddu,0x168bb014u},0.655444264f},
};

int main()
{
	int failures = 0;
	for (unsigned c = 0; c < sizeof(golden) / sizeof(golden[0]); c++)
	{
		const Golden &g = golden[c];
		WorldRandom r;
		r.Seed(g.seed, g.stream, g.index);
		for (int i = 0; i < 4; i++)
		{
			uint32_t v = r.Next();
			if (v != g.next[i])
			{
				printf("seed %u stream %u index %u, value %d: got 0x%08x, expected 0x%08x\n",
					g.seed, g.stream, g.index, i, v, g.next[i]);
				failures++;
			}
		}
		WorldRandom f;
		f.Seed(g.seed, g.stream, g.index);
		float x = f.Float();
		if (x != g.first || x < 0 || x >= 1)
		{
			printf("seed %u: Float() = %.9g, expected %.9g\n", g.seed, x, g.first);
			failures++;
		}
	}
	// Float() stays in [0, 1) over a long run
	WorldRandom r;
	r.Seed(1, 1, 1);
	for (int i = 0; i < 1000000; i++)
	{
		float x = r.Float();
		if (x < 0 || x >= 1)
		{
			printf("Float() out of [0,1): %.9g\n", x);
			failures++;
			break;
		}
	}
	printf(failures ? "worldrandom: %d failure(s)\n" : "worldrandom: OK\n", failures);
	return failures ? 1 : 0;
}
