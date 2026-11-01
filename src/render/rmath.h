//4x4 matrices of the renderer (M13): what the GL matrix stack and GLU computed, on the
//CPU, so a backend only receives finished matrices (sokol_gfx has no matrix stack).
//Column-major like OpenGL: m[col*4+row]. The formulas and the float/double mix
//follow the GL driver and the SGI GLU that the original code called, so the game
//draws with the same matrices to the last bit (pixel-diff, M13.2).

#ifndef RMATHH
#define RMATHH

#include <math.h>

//a*b+c*d... stay two roundings, as in the GL driver: a fused multiply-add (clang
//contracts by default on arm64) would move vertices by a fraction of an ulp. Per
//function, so the code that includes this header keeps its own float behaviour.
#if defined(__clang__)
#define RMATH_NOCONTRACT _Pragma("clang fp contract(off)")
#else
#define RMATH_NOCONTRACT
#endif

struct Mat4
{
	float m[16];
};

inline Mat4 Mat4Identity()
{
	Mat4 r={{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
	return r;
}

//a*b: b applied first (glMultMatrixf(b) with a current)
inline Mat4 Mat4Mul(const Mat4 &a,const Mat4 &b)
{
	RMATH_NOCONTRACT
	Mat4 r;
	for(int c=0;c<4;c++)
		for(int i=0;i<4;i++)
			r.m[c*4+i]=a.m[i]*b.m[c*4]+a.m[4+i]*b.m[c*4+1]+a.m[8+i]*b.m[c*4+2]+a.m[12+i]*b.m[c*4+3];
	return r;
}

//from a row-major float[4][4] as the game writes them (x axis, y axis, z axis,
//position), which is GL's column-major layout
inline Mat4 Mat4From(const float rows[4][4])
{
	Mat4 r;
	for(int i=0;i<16;i++) r.m[i]=rows[i/4][i%4];
	return r;
}

inline Mat4 Mat4Translate(float x,float y,float z)
{
	Mat4 r=Mat4Identity();
	r.m[12]=x;
	r.m[13]=y;
	r.m[14]=z;
	return r;
}

inline Mat4 Mat4Scale(float x,float y,float z)
{
	Mat4 r=Mat4Identity();
	r.m[0]=x;
	r.m[5]=y;
	r.m[10]=z;
	return r;
}

//glRotatef: degrees around the axis (x,y,z)
inline Mat4 Mat4Rotate(float deg,float x,float y,float z)
{
	RMATH_NOCONTRACT
	Mat4 r=Mat4Identity();
	float len=sqrtf(x*x+y*y+z*z);
	if(len==0) return r;
	x/=len;
	y/=len;
	z/=len;
	float a=deg*(float)(3.14159265358979323846/180);
	float c=cosf(a),s=sinf(a),t=1-c;
	r.m[0]=x*x*t+c;   r.m[4]=x*y*t-z*s; r.m[8]=x*z*t+y*s;
	r.m[1]=y*x*t+z*s; r.m[5]=y*y*t+c;   r.m[9]=y*z*t-x*s;
	r.m[2]=x*z*t-y*s; r.m[6]=y*z*t+x*s; r.m[10]=z*z*t+c;
	return r;
}

//gluPerspective: computed in double, then given to GL as floats
inline Mat4 Mat4Perspective(double fovy,double aspect,double znear,double zfar)
{
	Mat4 r=Mat4Identity();
	double rad=fovy/2*3.14159265358979323846/180;
	double dz=zfar-znear,sine=sin(rad);
	if(dz==0||sine==0||aspect==0) return r;
	double cotangent=cos(rad)/sine;
	r.m[0]=(float)(cotangent/aspect);
	r.m[5]=(float)cotangent;
	r.m[10]=(float)(-(zfar+znear)/dz);
	r.m[11]=-1;
	r.m[14]=(float)(-2*znear*zfar/dz);
	r.m[15]=0;
	return r;
}

//m*gluLookAt: a rotation built in float, then the translation to the eye, each
//multiplied into m in turn as GL did (m is what the matrix held before)
inline Mat4 Mat4LookAt(const Mat4 &m,double ex,double ey,double ez,double cx,double cy,double cz,double ux,double uy,double uz)
{
	float f[3]={(float)(cx-ex),(float)(cy-ey),(float)(cz-ez)};
	float up[3]={(float)ux,(float)uy,(float)uz};
	float s[3];
	struct L
	{
		static void Normalize(float v[3])
		{
			RMATH_NOCONTRACT
			float r=(float)sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
			if(r==0) return;
			v[0]/=r;
			v[1]/=r;
			v[2]/=r;
		}
		static void Cross(const float a[3],const float b[3],float out[3])
		{
			RMATH_NOCONTRACT
			out[0]=a[1]*b[2]-a[2]*b[1];
			out[1]=a[2]*b[0]-a[0]*b[2];
			out[2]=a[0]*b[1]-a[1]*b[0];
		}
	};
	L::Normalize(f);
	L::Cross(f,up,s);
	L::Normalize(s);
	L::Cross(s,f,up);
	Mat4 r=Mat4Identity();
	r.m[0]=s[0];  r.m[4]=s[1];  r.m[8]=s[2];
	r.m[1]=up[0]; r.m[5]=up[1]; r.m[9]=up[2];
	r.m[2]=-f[0]; r.m[6]=-f[1]; r.m[10]=-f[2];
	return Mat4Mul(Mat4Mul(m,r),Mat4Translate((float)-ex,(float)-ey,(float)-ez));
}

//gluProject: object to window coordinates, in double; 0 if behind the eye plane
inline int Mat4Project(double x,double y,double z,const Mat4 &modelview,const Mat4 &projection,
	const int viewport[4],double *wx,double *wy,double *wz)
{
	RMATH_NOCONTRACT
	double in[4]={x,y,z,1},out[4];
	for(int i=0;i<4;i++)
		out[i]=in[0]*modelview.m[i]+in[1]*modelview.m[4+i]+in[2]*modelview.m[8+i]+in[3]*modelview.m[12+i];
	for(int i=0;i<4;i++)
		in[i]=out[0]*projection.m[i]+out[1]*projection.m[4+i]+out[2]*projection.m[8+i]+out[3]*projection.m[12+i];
	if(in[3]==0) return 0;
	in[0]/=in[3];
	in[1]/=in[3];
	in[2]/=in[3];
	*wx=(in[0]*.5+.5)*viewport[2]+viewport[0];
	*wy=(in[1]*.5+.5)*viewport[3]+viewport[1];
	*wz=in[2]*.5+.5;
	return 1;
}

#endif
