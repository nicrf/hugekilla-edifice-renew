#include "math.h"
#include "vector.h"
Vector::Vector()
{
	x=y=z=0;
}
Vector::Vector(const float &xn,const float &yn,const float &zn)
{
	x=xn;
	y=yn;
	z=zn;
}
