//used to read structures (forts, ramps, etc.) and to place and build them them.


#include <StdAfx.h>
#include "land.h"
#include "redrawing.h"
#include "multiplay.h"

#define MAXSTRUCTSIZE 40
char curstructure[MAXSTRUCTSIZE][MAXSTRUCTSIZE][MAXSTRUCTSIZE];

extern char menutext[5000];
extern char menu;

char curstructurepath[1000];

//placement of the preview: where the player aims, up to MAXBUILDDIST blocks away
//(kept well within the server's ac_builddist), then turned by the wheel (extra
//quarter turns) and raised or lowered with Ctrl/Cmd + wheel (blocks)
#define MAXBUILDDIST 15
#define MAXBUILDHEIGHT 10
static int buildrot;
static int buildheight;

//the help shown while placing, with the current turn and height
void BuildPreviewText()
{
	sprintf(menutext,"Left click: build here\rRight click: cancel\r"
		"Wheel: rotate (%d)\rCtrl/Cmd + wheel: height (%+d)",buildrot*90,buildheight);
}

void BuildPreviewWheel(int dir,int height)
{
	if(height)
	{
		buildheight+=dir;
		if(buildheight<-MAXBUILDHEIGHT) buildheight=-MAXBUILDHEIGHT;
		if(buildheight>MAXBUILDHEIGHT) buildheight=MAXBUILDHEIGHT;
	}
	else buildrot=(buildrot+(dir>0?1:3))&3;
	BuildPreviewText();
}

//the land, or the top of the highest block, under (x,y,z)
static float GroundBelow(float x,float y,float z)
{
	float land=GetLandSmooth(x,z);
	int bx=(int)floorf(x/BLOCKSIZE),bz=(int)floorf(z/BLOCKSIZE);
	for(int by=(int)floorf(y/BLOCKSIZE);by>=-1&&(by+1)*BLOCKSIZE>land;by--)
	if(GetBlock(bx,by,bz)->type)
	{
		//GetHeight knows the slope of ramps
		float h=GetHeight(Vector(x,by*BLOCKSIZE+BLOCKSIZE*.99f,z));
		return h>land?h:land;
	}
	return land;
}

//where the player aims: along the view from the eye (the renderer's eyepos is the
//player's position) to the first land or block, at most MAXBUILDDIST blocks away.
//The last free point before the hit is dropped to the ground, so looking at a wall
//builds against it, not inside. Returns 0 (and the ground at max range) if nothing
//is hit in range.
static int BuildAim(Vector *aim)
{
	Vector eye=GetLocalPlayer()->pos;
	Vector dir=Normalize(GetLocalPlayer()->dir);
	const float step=.25f,range=MAXBUILDDIST*BLOCKSIZE;
	Vector last=eye;
	for(float d=step;d<=range;d+=step)
	{
		Vector p=eye+dir*d;
		if(p.y<=GetHeight(p))
		{
			*aim=Vector(last.x,GroundBelow(last.x,last.y,last.z),last.z);
			return 1;
		}
		last=p;
	}
	*aim=Vector(last.x,GroundBelow(last.x,last.y,last.z),last.z);
	return 0;
}


//after the player selects a structure or type, the filename is gotten with this function.

char *GetStructureFilename(int number)
{
	static char fullfilename[400];   //returned: was a local array
	std::vector<HKDirEntry> entries=HKListDir(curstructurepath);
	if(number<1||number>(int)entries.size()) return 0;
	const HKDirEntry &e=entries[number-1];
	if(!e.isdir)
	{
		snprintf(fullfilename,sizeof(fullfilename),"%s/%s",curstructurepath,e.name.c_str());
		return fullfilename;
	}
	strcat(curstructurepath,"/");
	strcat(curstructurepath,e.name.c_str());
	fullfilename[0]=0;
	return fullfilename;
}

//reads a structure

int LoadStructure(char *filename)
{
	int x,y,z;
	for(x=0;x<MAXSTRUCTSIZE;x++)
	{
		for(y=0;y<MAXSTRUCTSIZE;y++)
		{
			for(z=0;z<MAXSTRUCTSIZE;z++)
			{	
				curstructure[x][y][z]=' ';
			}
		}
	}
	if(!filename) return 0;
	long filelen;
	char *filecontent=HKReadFile(filename,&filelen);
	if(!filecontent) return 0;
	x=y=z=0;
	for(int i=0;i<filelen;i++)
	{
		if(filecontent[i]=='\r')
		{
			i+=2;
			x=0;
			z++;
			if(filecontent[i]=='\r')
			{
				i+=2;
				z=0;
				y++;
			}
		}
		if(i>=filelen) break;
		curstructure[x][y][z]=filecontent[i];
		x++;
	}
	delete []filecontent;
	return 1;
}

//if the player turns around, then the building needs to turn with him

void RotateStructure(char o[MAXSTRUCTSIZE][MAXSTRUCTSIZE][MAXSTRUCTSIZE],
					 char n[MAXSTRUCTSIZE][MAXSTRUCTSIZE][MAXSTRUCTSIZE])
{
	int x,y,z;
	char nval;
	for(x=0;x<MAXSTRUCTSIZE;x++)
	{
		for(y=0;y<MAXSTRUCTSIZE;y++)
		{
			for(z=0;z<MAXSTRUCTSIZE;z++)
			{
				nval=o[z][y][x];
				switch(nval)
				{
				case '>':
					nval='v';
					break;
				case 'v':
					nval='<';
					break;
				case '<':
					nval='^';
					break;
				case '^':
					nval='>';
					break;
				default:
					break;
				}
				n[(MAXSTRUCTSIZE-1)-x][y][z]=nval;
			}
		}
	}
}

//middle of the blocks of a structure, seen from above
static void FootprintCenter(char s[MAXSTRUCTSIZE][MAXSTRUCTSIZE][MAXSTRUCTSIZE],float *cx,float *cz)
{
	int minx=MAXSTRUCTSIZE,maxx=-1,minz=MAXSTRUCTSIZE,maxz=-1;
	for(int x=0;x<MAXSTRUCTSIZE;x++)
	for(int y=0;y<MAXSTRUCTSIZE;y++)
	for(int z=0;z<MAXSTRUCTSIZE;z++)
	if(s[x][y][z]!=' '&&s[x][y][z]!='r')
	{
		if(x<minx) minx=x;
		if(x>maxx) maxx=x;
		if(z<minz) minz=z;
		if(z>maxz) maxz=z;
	}
	if(maxx<0) minx=maxx=minz=maxz=0;
	*cx=(minx+maxx)/2.f;
	*cz=(minz+maxz)/2.f;
}

//has two purposes: one, draw the structure as a wireframe (red where it can't be
//built).  two, place the structure permenantly; returns 0 if it can't be built there

int PlaceStructure(int task)
{
	int offx,offy,offz;
	int x,y,z;
	int rot;
	char rotstructure[MAXSTRUCTSIZE][MAXSTRUCTSIZE][MAXSTRUCTSIZE];
	char tmpstructure[MAXSTRUCTSIZE][MAXSTRUCTSIZE][MAXSTRUCTSIZE];
	Vector pdir=GetLocalPlayer()->dir;
	Vector ppos=GetLocalPlayer()->pos;
	Vector aim;
	int inrange=BuildAim(&aim);
	if(task==1&&!inrange) return 0;
	char red=!inrange||!CanBuild();
	if(fabs(pdir.x)>fabs(pdir.z))
	{
		if(pdir.x>0) rot=0;
		else rot=2;
	}
	else
	{
		if(pdir.z>0) rot=1;
		else rot=3;
	}
	//facing the player, then turned by the wheel
	CopyMemory(rotstructure,curstructure,sizeof(curstructure));
	for(int i=0;i<rot+buildrot;i++)
	{
		RotateStructure(rotstructure,tmpstructure);
		CopyMemory(rotstructure,tmpstructure,sizeof(rotstructure));
	}
	//its 'r' blocks give its height: as high above the aimed ground as they were
	//above the ground under a standing player (eye about 1.5 over it)
	float refy=0;
	int numref=0;
	for(x=0;x<MAXSTRUCTSIZE;x++)
	for(y=0;y<MAXSTRUCTSIZE;y++)
	for(z=0;z<MAXSTRUCTSIZE;z++)
	if(rotstructure[x][y][z]=='r')
	{
		refy+=y;
		numref++;
	}
	if(numref) refy/=numref;
	//the middle of its footprint on the aimed point, so the wheel turns it in place
	float cx,cz;
	FootprintCenter(rotstructure,&cx,&cz);
	offx=(int)floorf(aim.x/BLOCKSIZE-cx)+BLOCKGRIDSIZE;
	offy=(aim.y+1.5f)/BLOCKSIZE-refy-.5;
	offz=(int)floorf(aim.z/BLOCKSIZE-cz)+BLOCKGRIDSIZE;
	offy+=buildheight;
	int px,py,pz;
	px=ppos.x;
	py=ppos.y;
	pz=ppos.z;
	char top=RND*10,postside=RND*10,side=RND*10,bottom=RND*10;
	if(task==1)
	{
		StartBlockBuffer();
	}
	for(x=0;x<MAXSTRUCTSIZE;x++)
	for(y=0;y<MAXSTRUCTSIZE;y++)
	for(z=0;z<MAXSTRUCTSIZE;z++)
	{
		char cb=rotstructure[x][y][z];
		if(task==0)
		switch (cb)
		{
			case 'p':
				{
					for(int i=y+offy;i>=0;i--)
					{
						DrawBlock(x+offx-BLOCKGRIDSIZE,i,z+offz-BLOCKGRIDSIZE,255,1,px,py,pz,red);
						if(GetBlock(x+offx-BLOCKGRIDSIZE,i,z+offz-BLOCKGRIDSIZE)->type==1) break;
					}
				}

			case 'x':		
			DrawBlock(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,255,1,px,py,pz,red);
			break;
			case '>':
				DrawBlock(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,255,4,px,py,pz,red);
			break;
			case 'v':
				DrawBlock(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,255,3,px,py,pz,red);
			break;
			case '<':
				DrawBlock(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,255,2,px,py,pz,red);
			break;
			case '^':
				DrawBlock(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,255,5,px,py,pz,red);
			break;
		}
		int submersed=1;
		if(task==1&&cb!=' ')
		{
			for(int lx=x*BLOCKSIZE;lx<x*BLOCKSIZE+BLOCKSIZE-1;lx++)
			for(int lz=z*BLOCKSIZE;lz<z*BLOCKSIZE+BLOCKSIZE-1;lz++)
			{
				if(*GetRepeat(lx+offx*BLOCKSIZE,lz+offz*BLOCKSIZE)<(y+offy)*BLOCKSIZE+BLOCKSIZE*2+2)
				{
					submersed=0;
					break;
				}
			}
		}
		

		if(task==1&&!submersed)
		switch (cb)
		{
			case 'p':
				{
					for(int i=y+offy;i>=0;i--)
					{
						int type=GetBlock(x+offx-BLOCKGRIDSIZE,i,z+offz-BLOCKGRIDSIZE)->type;
						AddBlockBuffer(x+offx-BLOCKGRIDSIZE,i,z+offz-BLOCKGRIDSIZE,1,0,top,postside,bottom,0);
						if(type==1) break;
					}
				}
			case 'x':		
				AddBlockBuffer(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,1,0,top,side,bottom,0);
				break;
			case 'd':		
				AddBlockBuffer(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,1,0,top,side,bottom,1);
			break;
			case '>':
				AddBlockBuffer(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,4,0,top,side,bottom,0);
			break;
			case 'v':
				AddBlockBuffer(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,3,0,top,side,bottom,0);
			break;
			case '<':
				AddBlockBuffer(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,2,0,top,side,bottom,0);
			break;
			case '^':
				AddBlockBuffer(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,5,0,top,side,bottom,0);
			break;
		}
	}
	if(task==1)
	{
		SendBlockBuffer();
	}
	return 1;
}


//used to display a directory full of structures

void DisplayBuildingType(char *path)
{
	strcpy(menutext,"Select structure or type:\r\r");
	if(path) strcpy(curstructurepath,path);
	std::vector<HKDirEntry> entries=HKListDir(curstructurepath);
	for(int i=0;i<(int)entries.size()&&i<9;i++)
	{
		char name[300];
		snprintf(name,sizeof(name),"%s",entries[i].name.c_str());
		char *dot=strchr(name,'.');
		if(dot) *dot=0;
		itoa(i+1,&menutext[strlen(menutext)],10);
		strcat(menutext,". ");
		strcat(menutext,name);
		if(entries[i].isdir)
			strcat(menutext," (Dir)");
		strcat(menutext,"\r");
	}
}


