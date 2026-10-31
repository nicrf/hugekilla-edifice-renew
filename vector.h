#ifndef _VECTOR
#define _VECTOR
#include "math.h"

class Vector
{
public:
	float x,y,z;
	Vector();
	Vector(const float &xn,const float &yn,const float &zn);
};

inline Vector& operator+=(Vector &a,Vector &b)
{
	a.x+=b.x;
	a.y+=b.y;
	a.z+=b.z;
	return a;
}

inline Vector& operator-=(Vector &a,Vector &b)
{
	a.x-=b.x;
	a.y-=b.y;
	a.z-=b.z;
	return a;
}
inline Vector& operator*=(Vector &a,Vector &b)
{
	a.x*=b.x;
	a.y*=b.y;
	a.z*=b.z;
	return a;
}

inline Vector& operator*=(Vector &a,float b)
{
	a.x*=b;
	a.y*=b;
	a.z*=b;
	return a;
}
inline Vector& operator/=(Vector &a,float b)
{
	a.x/=b;
	a.y/=b;
	a.z/=b;
	return a;
}

inline Vector operator+(Vector &a,Vector &b)
{
	Vector ret;
	ret.x=a.x+b.x;
	ret.y=a.y+b.y;
	ret.z=a.z+b.z;
	return ret;
}
inline Vector operator-(Vector &a,Vector &b)
{
	Vector ret;
	ret.x=a.x-b.x;
	ret.y=a.y-b.y;
	ret.z=a.z-b.z;
	return ret;
}
inline Vector operator*(Vector &a,Vector &b)
{
	Vector ret;
	ret.x=a.x*b.x;
	ret.y=a.y*b.y;
	ret.z=a.z*b.z;
	return ret;
}

inline Vector operator*(Vector &a,float b)
{
	Vector ret;
	ret.x=a.x*b;
	ret.y=a.y*b;
	ret.z=a.z*b;
	return ret;
}

inline Vector operator/(Vector &a,float b)
{
	Vector ret;
	ret.x=a.x/b;
	ret.y=a.y/b;
	ret.z=a.z/b;
	return ret;
}

inline Vector operator*(float b,Vector &a)
{
	Vector ret;
	ret.x=a.x*b;
	ret.y=a.y*b;
	ret.z=a.z*b;
	return ret;
}

inline Vector operator/(float b,Vector &a)
{
	Vector ret;
	ret.x=b/a.x;
	ret.y=b/a.y;
	ret.z=b/a.z;
	return ret;
}

inline float DotProduct(Vector &a,Vector &b)
{
	return a.x*b.x+a.y*b.y+a.z*b.z;
}

inline float Magnitude(Vector &a)
{
	return (float)sqrt(a.x*a.x+a.y*a.y+a.z*a.z);
}

inline Vector VectMult(Vector &a,Vector &x,Vector &y,Vector &z)
{
	Vector ret;
	ret=a.x*x;
	ret+=a.y*y;
	ret+=a.z*z;
	return ret;
}

inline Vector Normalize(Vector &a)
{
	Vector ret;
	float mag=Magnitude(a);
	if (mag)
	{
		ret=a/mag;
	}
	else
	{
		ret.x=1;
	}
	return ret;
}

inline Vector CrossProduct(Vector &a,Vector &b)
{
	Vector ret;
	ret.x=a.y*b.z-a.z*b.y;
	ret.y=a.z*b.x-a.x*b.z;
	ret.z=a.x*b.y-a.y*b.x;
	return ret;
}

// get a vector perpendicular to a in the direction of b
inline Vector GetRight(Vector &a,Vector &b)
{
	Vector ret;
	ret=Normalize(CrossProduct(CrossProduct(a,b),a));
	return ret;
}
#endif