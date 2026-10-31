inline float *GetRepeat(int x,int y)
{
	x&=LANDGRIDSIZE-1;
	y&=LANDGRIDSIZE-1;
	return &land[x][y];
}
inline void SetLandLight(int x, int y)
{
	int xs=x*4+cloudposx*1;
	int ys=y*4+cloudposy*1;
	x&=LANDGRIDSIZE-1;
	y&=LANDGRIDSIZE-1;
	float l=lighting[x][y];
	
	
	if(skysettings.sunheight&&skysettings.uselandclouds)
	{
		ys-=*GetRepeat(x,y)/skysettings.sunheight;
		xs*=1;
		ys*=1;
		xs-=(xs/cloudsize)*cloudsize;
		ys-=(ys/cloudsize)*cloudsize;
		if(xs<0) xs+=cloudsize;
		if(ys<0) ys+=cloudsize;
		
		float nf;
		if(clouds)
		nf=((float)*((BYTE*)&clouds[xs*cloudsize+ys]))/256;
		l*=nf;
		if(l<.3)l=.3;
	}
	glColor4f(l,l,l,1);
}

