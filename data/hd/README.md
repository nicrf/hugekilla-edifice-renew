# HD textures of the Ultra graphics

Loaded only when Options → Video/Audio → Graphics is **Ultra** (`graphics=ultra` in
`control.ini`), by `src/render/renderer_ultra.cpp`; Classic never reads this folder,
and the web build leaves it out.

Every albedo was generated on 2026-11-01 with
[black-forest-labs/flux-schnell](https://replicate.com/black-forest-labs/flux-schnell)
on Replicate (4 steps, 1 megapixel, square). FLUX.1 [schnell] is released under the
**Apache License 2.0**
([LICENSE-FLUX1-schnell](https://github.com/black-forest-labs/flux/blob/main/model_licenses/LICENSE-FLUX1-schnell)),
which puts no restriction on the outputs: they are redistributed here under the
same Apache-2.0 terms (see `licenses/hd-textures.txt`), alongside the game's LGPL.

`tools/hd_textures.py generate` asks for them (the API token comes from
`REPLICATE_API_TOKEN`, never stored), `tools/hd_textures.py process` makes them
tileable (blended with their half-shifted copy near the edges), flattens the light
left in the photo, resizes them to 1024×1024 and derives the two other maps, which
no generator gives:

- `<name>.jpg`: albedo (sRGB);
- `<name>_n.jpg`: tangent-space normal (Sobel of a height taken from the luminance);
- `<name>_r.jpg`: R ambient occlusion, G roughness, B height.

Each prompt ends with: *seamless tileable texture, orthographic top-down view, flat
even diffuse lighting, no shadows, no perspective, no objects, photorealistic PBR
albedo, highly detailed* (except `grass` and `grass2`, whose whole prompt is given).

| File | Used for | Prompt (start) | Seed |
|---|---|---|---|
| `grass` | land, grass blades' color | flat lay texture photograph of uniform short mown lawn grass, evenly distributed tiny blades, no focal point, no center, some dry yellowish blades and small weeds, overcast diffuse light, seamless tileable, no objects, no text, high resolution | 71 |
| `grass2` | land (second grass, mixed at a large scale) | flat lay texture photograph of uniform rough pasture ground seen straight down, short sparse grass, moss, dry straw and brown earth evenly mixed, no focal point, overcast diffuse light, seamless tileable, no objects, no text, high resolution | 72 |
| `rock` | land (slopes, triplanar) | natural rough weathered mountain rock, grey granite boulders surface with fine grain, small moss patches | 32 |
| `sand` | land (shores, lake beds) | close-up photograph of coarse beach sand grains with small shells and pebbles | 33 |
| `snow` | land (high ground, snowy maps) | fresh white snow surface with soft wind ripples | 14 |
| `dirt` | land (patches) | close-up photograph of dry brown soil ground with small stones and fine cracks | 35 |
| `foam` | water (shore and crests) | white sea foam bubbles on dark water | 16 |
| `wall0` | block texture wall0 | grey stone tile wall made of rectangular slate blocks of different sizes with dark mortar | 20 |
| `wall1` | wall1 | close-up photograph of rough pale olive green painted stucco wall with grain and small cracks | 41 |
| `wall2` | wall2 | close-up photograph of light grey concrete wall with pores, small holes and water stains | 42 |
| `wall3` | wall3 | weathered grey wooden planks, horizontal grain | 23 |
| `wall4` | wall4 | rusty orange corroded steel plate | 24 |
| `wall5` | wall5 | grey stone tile wall made of rectangular slate blocks of different sizes with dark mortar | 25 |
| `wall6` | wall6 | ancient carved sandstone blocks with glyphs, olive and ochre | 26 |
| `wall7` | wall7 | close-up photograph of dark grey rough cast concrete with visible aggregate stones | 47 |
| `wall8` | wall8 | close-up photograph of dark grey concrete panel crossed by one horizontal black rubber stripe | 48 |
| `wall9` | wall9 | olive brown wet mud and stone | 29 |

Generations used: 27 (the 17 kept, and 10 discarded tries: a first grass, rock,
sand, dirt, wall1, wall2, wall7, wall8 that looked drawn or flat, and two grasses
with a visible center or perspective).
