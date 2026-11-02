//The renderer (M13): what the game asks of the GPU, without OpenGL.
//
//The game draws in immediate mode, as it did with glBegin/glEnd: vertices with the
//current color and texture coordinate go into a batch, and the batch is drawn (one
//backend call for many primitives) only when a state it depends on changes, or when
//the frame is read or presented. renderer.cpp keeps the state and the batch; the
//backend (rbackend.h) draws a batch with one complete state: renderer_gl.cpp with
//fixed-function OpenGL, and later sokol_gfx (M13.3), where a state is a pipeline and
//uniforms, and fog becomes a shader uniform.
//
//The state is OpenGL's, the parts the game uses: what a backend has to reproduce for
//the game to look the same.

#ifndef RENDERERH
#define RENDERERH

#include "rmath.h"

//---- geometry ----

enum RPrim
{
	RP_POINTS,
	RP_LINES,
	RP_LINESTRIP,
	RP_LINELOOP,
	RP_TRIANGLES,
	RP_TRISTRIP,
	RP_TRIFAN,
	RP_QUADS        //4 vertices each; a backend without quads splits them (0 1 2, 0 2 3)
};

struct RVertex
{
	float x,y,z;
	float u,v;
	float r,g,b,a;
	float nx,ny,nz;       //the model's normal (Ultra graphics only; 0,0,0: none)
};

//a primitive (or a run of them) between RBegin and REnd, as glBegin/glEnd did:
//RVertex* takes the current color and texture coordinate. Incomplete primitives
//(a lone vertex of a triangle...) are dropped. State changes go outside.
void RBegin(RPrim prim);
void REnd();

//the batch and the current vertex, inline: the land pushes some 100k vertices a frame
struct RBatch
{
	RVertex cur;          //current color and texture coordinate
	RVertex *verts;
	int count,capacity;
};
extern RBatch rbatch;
void RGrowBatch();

inline void RColor4f(float r,float g,float b,float a)
{
	rbatch.cur.r=r;
	rbatch.cur.g=g;
	rbatch.cur.b=b;
	rbatch.cur.a=a;
}
inline void RColor3f(float r,float g,float b)
{
	RColor4f(r,g,b,1);
}
inline void RTexCoord2f(float u,float v)
{
	rbatch.cur.u=u;
	rbatch.cur.v=v;
}
inline void RVertex3f(float x,float y,float z)
{
	if(rbatch.count==rbatch.capacity) RGrowBatch();
	RVertex &v=rbatch.verts[rbatch.count++];
	v=rbatch.cur;
	v.x=x;
	v.y=y;
	v.z=z;
}
inline void RVertex2f(float x,float y)
{
	RVertex3f(x,y,0);
}
inline void RVertex3fv(const float *p)
{
	RVertex3f(p[0],p[1],p[2]);
}

//whole primitives with their own colors and texture coordinates (the current ones
//don't change)
void RPushPoint(const RVertex &v);
void RPushLine(const RVertex v[2]);
void RPushTri(const RVertex v[3]);
void RPushQuad(const RVertex v[4]);

//a model (.cob, AseObj::Draw): triangles in runs of one texture, drawn with the
//modelview times model. A run with texture RMESH_KEEPTEX keeps the bound texture.
#define RMESH_KEEPTEX (-1)
struct RMeshPart
{
	int texture;          //registry slot (textures.h)
	int first,count;      //vertices, 3 per triangle
};
void RDrawMesh(const Mat4 &model,const RVertex *verts,const RMeshPart *parts,int numparts);

//---- state ----

//features turned on and off (glEnable): bits for REnable/RDisable
enum
{
	RCAP_TEXTURE=1,       //modulate the vertex color by the bound texture
	RCAP_BLEND=2,
	RCAP_DEPTHTEST=4,     //less-than test against the depth buffer
	RCAP_CULL=8,          //drops the faces RCullFace names (counter-clockwise = front)
	RCAP_FOG=16,          //linear fog by eye distance (RFogRange, RFogColor)
	RCAP_SCISSOR=32,      //draw and clear only inside RScissor
	RCAP_LINESMOOTH=64    //antialiased lines
};
void REnable(int caps);
void RDisable(int caps);

//color blending when RCAP_BLEND is on: source and destination factors
enum RBlend
{
	RBLEND_OPAQUE,        //ONE, ZERO (GL's default)
	RBLEND_ALPHA,         //SRC_ALPHA, ONE_MINUS_SRC_ALPHA
	RBLEND_ADD,           //SRC_ALPHA, ONE: glows (sun, explosions, particles)
	RBLEND_ADDONE,        //ONE, ONE
	RBLEND_SCALE          //SRC_ALPHA, ZERO: color times its alpha
};
void RBlendFunc(RBlend blend);

enum RFace
{
	RFACE_BACK,
	RFACE_FRONT
};
void RCullFace(RFace face);
void RDepthMask(int write);   //depth writes, on by default
void RLineWidth(float pixels);
void RPointSize(float pixels);
void RFogRange(float start,float end);
void RFogColor(const float rgba[4]);
//the bound texture is TexBind (textures.h)

//matrices: the projection, and the modelview (camera, then the model drawn). The 2D
//pass (menus, HUD, console) draws in normalized device coordinates, -1..1 on both
//axes, with both matrices the identity (RSet2D); the 3D pass sets a perspective.
void RSetProjection(const Mat4 &m);
void RSetModelView(const Mat4 &m);
const Mat4 &RProjection();
const Mat4 &RModelView();
void RSet2D();

//the drawing area in pixels of the window's drawable (x, y from the bottom left)
void RViewport(int x,int y,int w,int h);
void RGetViewport(int vp[4]);
void RScissor(int x,int y,int w,int h);

//glPushAttrib/glPopAttrib: saves the named groups of state, which RPopAttrib restores
enum
{
	RATTR_ENABLE=1,       //every RCAP_*
	RATTR_CURRENT=2,      //current color and texture coordinate
	RATTR_COLOR=4,        //blend on/off and function, clear color
	RATTR_FOG=8,          //fog on/off, range and color
	RATTR_POLYGON=16,     //culling on/off and face
	RATTR_TEXTURE=32      //bound texture
};
void RPushAttrib(int mask);
void RPopAttrib();

//---- frame ----

void RInit();                 //after the context is created
void RClearColor(float r,float g,float b,float a);
void RClear(int color,int depth);
void RFlush();                //draws the batch: before presenting the frame

//reads back the frame being drawn: rows bottom-up and packed (no padding), 3 bytes a
//pixel, RGB or BGR (HK_SHOT, the screenshot key); and the depth at one pixel, 0..1
//(the sun flare looks for what hides the sun)
void RReadPixels(int x,int y,int w,int h,int bgr,unsigned char *out);
void RReadDepth(int x,int y,float *depth);   //outside the window: may leave *depth as it was

//---- Ultra graphics (sokol_gfx backend only) ----
//
//An optional render path over the same calls: the 3D scene goes into an HDR image,
//lit by the sun (PBR, cascaded shadows), with its own sky, water and grass, then
//post-processed (bloom, god rays, ACES tonemapping, FXAA) into the frame, under
//the HUD drawn as before. The game only says what it draws (RSetMaterial) and
//where the scene starts and ends; with Ultra off every call below does nothing,
//so Classic draws exactly what it did.

int RUltraSupported();        //the backend can (sokol_gfx Metal, D3D11, GL core; not WebGL2, not --gl)
void RSetUltra(int on);       //Options' Graphics: Classic 0, Ultra 1
int RUltra();                 //on and supported

//sub-options (Options, Ultra rows): shadows 0 off, 1 two cascades, 2 four;
//grass 0 off, 1 low, 2 high; water 0 sky reflection only, 1 planar reflection;
//vegetation (trees, bushes, rocks) 0 off, 1 near, 2 far; clouds 0 the map's
//layers, 1 volumetric
struct RUltraPrefs
{
	int shadows,grass,water,vegetation,clouds;
};
extern RUltraPrefs rultraprefs;

//what the next draws are: the Ultra shaders light them by kind (the vertex color,
//the classic baked light, is then ignored)
enum RMaterial
{
	RMAT_NONE,            //drawn as Classic does (in the HDR scene: particles, effects, clouds, text)
	RMAT_TERRAIN,         //heightmap land: splatted grass, rock, sand, snow, dirt
	RMAT_BLOCK,           //building blocks (TEX_BLOCK0+i: HD wall<i> if there is one)
	RMAT_MODEL,           //.cob models, with their normals
	RMAT_FOLIAGE,         //models that sway in the wind (trees, flowers)
	RMAT_SMOKE            //smoke puffs: lit by the sun and the sky, soft where they meet the scene
};
void RSetMaterial(int material);

//the 3D scene's frame: the camera alone (world to eye, without the model), the
//sun, the map's fog color and the land
struct RUltraScene
{
	Mat4 view;
	float eye[3];
	float sundir[3];      //towards the sun, normalized
	float fogcolor[3];    //the map's (skies.ini), sets the sky's mood
	float fogend;         //where the land ends
	float waterheight;
	float time;           //seconds, for the waves and the wind
	int underwater;
	const float *land;    //heights, land[x*landsize+z], the world repeating every landsize
	int landsize;
	//no tree nor rock there: around these points (flags, stations), nor where
	//blocked says a block is (x, y, z in the world)
	float avoid[32][3];
	int numavoid;
	int (*blocked)(float x,float y,float z);
};
void RUltraBegin(const RUltraScene &scene);
void RUltraWater();           //the water surface, here in the drawing order
void RUltraGrass();           //the grass on the land near the camera
void RUltraEnd();             //the scene is post-processed into the frame: the HUD comes next

#endif
