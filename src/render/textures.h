#ifndef TEXTURESH
#define TEXTURESH

//Texture registry (M12.5). The game names its textures by slot; the slot numbers
//are the IDs the original code passed straight to glBindTexture, kept so that
//textures sharing a slot (the splash and the first ground texture) still do.
//The GL names come from glGenTextures, so M13 can swap the backend here.
enum TexSlot
{
	TEX_LAND=1,             //DrawLand's first bind (blank until F2 loads a random ground)
	TEX_CLOUDS=2,           //sky: cloud layer (skies.ini cloudtex)
	TEX_SUN=3,
	TEX_SUNBACK=4,
	TEX_CROSSHAIR=5,
	TEX_CLOUDMIST=6,        //sky: mist layer (skies.ini cloudmist)
	TEX_WATER=7,
	TEX_AMMOICON=10,
	TEX_HEALTHICON=11,
	TEX_GUNNAMES=15,
	TEX_CREDIT0=20,         //HUD credit icons, TEX_CREDIT0+kind (20 build, 21 station move)
	TEX_SPRITE0=30,         //textures/sprite/*, in directory order (GetTexID)
	TEX_EXPLOSION0=100,     //gun i: TEX_EXPLOSION0+2i, its side view +1 (inside the sprite range past 70 sprites, as before)
	TEX_FONT=300,           //HUD, UI and console font, 16x16 glyphs
	TEX_BLOCK0=400,         //building blocks: textures/block/wall<i>.bmp
	TEX_LILLYPAD=600,
	TEX_GROUND0=800,        //ground textures of the land, TEX_GROUND0+i
	TEX_SPLASH=TEX_GROUND0, //startup.bmp; the first ground texture replaces it once a land loads
	TEX_MENU0=1300,         //main menu buttons, TEX_MENU0+button texture
	TEX_NUMSLOTS=1400
};

enum TexFilter
{
	TEXF_MIPNEAREST,        //.bmp files: linear within a mip level, nearest level
	TEXF_MIPLINEAR          //.pxr files: trilinear
};

unsigned TexName(int slot);     //backend name of the slot, created on first use
void TexBind(int slot);         //binds the slot to GL_TEXTURE_2D
//uploads 8-bit pixels (1 luminance, 2 luminance+alpha, 3 RGB, 4 RGBA channels),
//rows bottom-up, with mipmaps; replaces what the slot held
void TexUpload(int slot,int width,int height,int channels,const void *pixels,TexFilter filter);
void TexShutdown();             //frees every texture, before the GL context goes

//image files (image.cpp, stb_image: .bmp, .png, .tga) to 8-bit pixels, top row of the
//picture first (TexUpload's first row), channels per pixel as asked (1 gray, 3 RGB,
//4 RGBA); 0 if missing or unreadable
unsigned char *LoadImagePixels(const char *filename,int *width,int *height,int channels);
void FreeImagePixels(unsigned char *pixels);

//alpha of the font (TEX_FONT), one byte a pixel, rows as uploaded: kept from the
//upload so the UI measures its glyphs (ui.cpp) without reading the texture back,
//which sokol_gfx can't. 255 everywhere for a font without alpha; 0 before it loads.
const unsigned char *TexFontAlpha(int *width,int *height);

//file loaders (textures.cpp): an image file, or .pxr (raw, may carry alpha)
int LoadTexture(char *filename,unsigned slot);
int LoadRaw(char *filename,unsigned slot);
void LoadStartupTextures();     //font and splash, before the first frame
void LoadSplashTexture();       //startup.bmp back into TEX_SPLASH (disconnect, new land)

#endif
