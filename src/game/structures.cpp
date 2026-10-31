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


//after the player selects a structure or type, the filename is gotten with this function.

char *GetStructureFilename(int number)
{
	WIN32_FIND_DATA fd;
	char filter[500];
	strcpy(filter,curstructurepath);
	strcat(filter,"\\*.*");
	HANDLE findhand=FindFirstFile(filter,&fd);
	if(findhand==INVALID_HANDLE_VALUE) return 0;
	int found;
	int curstruct=0;
	do
	{
		if(!(strcmp(fd.cFileName,".")==0||strcmp(fd.cFileName,"..")==0))
		{
			curstruct++;
			if(curstruct==number) 
			{
				if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))
				{		
					char fullfilename[400];
					strcpy(fullfilename,curstructurepath);
					strcat(fullfilename,"\\");
					strcat(fullfilename,fd.cFileName);
					return fullfilename;
				}
				else
				{
					char fullfilename[400];
					strcat(curstructurepath,"\\");
					strcat(curstructurepath,fd.cFileName);
					fullfilename[0]=0;
					return fullfilename;
				}
			}
		}
		found=FindNextFile(findhand,&fd);
	}
	while (found);
	return 0;
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
	HANDLE file=CreateFile(filename,GENERIC_READ,0,0,OPEN_EXISTING,0,0);
	int filelen=GetFileSize(file,0);
	if(filelen==-1) return 0;
	char *filecontent=new char[filelen];
	DWORD read;
	ReadFile(file,filecontent,filelen,&read,0);
	CloseHandle(file);
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

//has two purposes: one, draw the structure as a wireframe.  two, place the structure permenantly

void PlaceStructure(int task)
{
	int offx,offy,offz;
	int x,y,z;
	int rot;
	char rotstructure[MAXSTRUCTSIZE][MAXSTRUCTSIZE][MAXSTRUCTSIZE];
	char tmpstructure[MAXSTRUCTSIZE][MAXSTRUCTSIZE][MAXSTRUCTSIZE];
	Vector pdir=GetLocalPlayer()->dir;
	Vector ppos=GetLocalPlayer()->pos;
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
	CopyMemory(rotstructure,curstructure,sizeof(curstructure));
	for(int i=0;i<rot;i++)
	{
		RotateStructure(rotstructure,tmpstructure);
		CopyMemory(rotstructure,tmpstructure,sizeof(rotstructure));
	}
	float refx,refy,refz;
	int numref=0;
	refx=refy=refz=0;
	for(x=0;x<MAXSTRUCTSIZE;x++)
	for(y=0;y<MAXSTRUCTSIZE;y++)
	for(z=0;z<MAXSTRUCTSIZE;z++)
	if(rotstructure[x][y][z]=='r')
	{
		refx+=x;
		refy+=y;
		refz+=z;
		numref++;
	}
	refx/=numref;
	refy/=numref;
	refz/=numref;
	offx=ppos.x/BLOCKSIZE-refx+BLOCKGRIDSIZE;
	offy=ppos.y/BLOCKSIZE-refy-.5;
	offz=ppos.z/BLOCKSIZE-refz+BLOCKGRIDSIZE;
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
						DrawBlock(x+offx-BLOCKGRIDSIZE,i,z+offz-BLOCKGRIDSIZE,255,1,px,py,pz);
						if(GetBlock(x+offx-BLOCKGRIDSIZE,i,z+offz-BLOCKGRIDSIZE)->type==1) break;
					}
				}

			case 'x':		
			DrawBlock(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,255,1,px,py,pz);
			break;
			case '>':
				DrawBlock(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,255,4,px,py,pz);
			break;
			case 'v':
				DrawBlock(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,255,3,px,py,pz);
			break;
			case '<':
				DrawBlock(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,255,2,px,py,pz);
			break;
			case '^':
				DrawBlock(x+offx-BLOCKGRIDSIZE,y+offy,z+offz-BLOCKGRIDSIZE,255,5,px,py,pz);
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

}


//used to display a directory full of structures

void DisplayBuildingType(char *path)
{
	strcpy(menutext,"Select structure or type:\r\r");
	if(path) strcpy(curstructurepath,path);
	WIN32_FIND_DATA fd;
	char filter[500];
	strcpy(filter,curstructurepath);
	strcat(filter,"\\*.*");
	HANDLE findhand=FindFirstFile(filter,&fd);
	if(findhand==INVALID_HANDLE_VALUE) return;
	int found;
	int curstruct=0;
	do
	{
		if(!(strcmp(fd.cFileName,".")==0||strcmp(fd.cFileName,"..")==0))
		{
			curstruct++;
			for(int i=0;i<strlen(fd.cFileName);i++)
			{
				if(fd.cFileName[i]=='.') fd.cFileName[i]=0;
			}
			itoa(curstruct,&menutext[strlen(menutext)],10);
			strcat(menutext,". ");
			strcat(menutext,fd.cFileName);
			if(fd.dwFileAttributes==FILE_ATTRIBUTE_DIRECTORY)
				strcat(menutext," (Dir)");
			strcat(menutext,"\r");
		}
		found=FindNextFile(findhand,&fd);
	}
	while (found&&curstruct<9);
}


