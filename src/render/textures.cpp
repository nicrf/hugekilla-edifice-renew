//used to load various textures from .bmp and .pxr files  (pxr files can store an alpha channel)

#include "StdAfx.h"
#include "redrawing.h"
#include "stdio.h"

#define MAXSPRITETEX 100


extern GunDrawInfo gun[10];
extern AseObj scenery[MAXSCENERY];
extern AseObj plobj[MAXSCENERY];
extern AseObj projectile[10];
extern int numscenery;
extern AseObj servercobs[50][3];
extern AseObj hangglider;

AseObj torso[2][10];
AseObj leg[5];

char texname[MAXSPRITETEX][400];
char texused[400];
int reflecttexnum;
int numtex;

int numpost;

int LoadTexture(char *filename,UINT num)
{
#ifdef HKDS 
	return 0;
#endif
	if(stricmp(&filename[strlen(filename)-4],".pxr")==0)
	{
		return LoadRaw(filename,num);;
	}
	
	void *himage=LoadImage(NULL,filename,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION|LR_LOADFROMFILE);
	BITMAP bm;
	if(!GetObject(himage,sizeof(bm),&bm)) 
	{
		return 0;
	}
	glBindTexture(GL_TEXTURE_2D,num);
	char *bits=(char *)bm.bmBits;

	char *newbits=new char[bm.bmHeight*bm.bmWidth*3];

	int totalsize=bm.bmWidth*bm.bmHeight;
	for(int y=0;y<bm.bmHeight;y++)
	for(int x=0;x<bm.bmWidth;x++)
	{
		newbits[y*bm.bmWidth*3+x*3]=bits[(bm.bmHeight-y-1)*bm.bmWidth*3+x*3+2];
		newbits[y*bm.bmWidth*3+x*3+1]=bits[(bm.bmHeight-y-1)*bm.bmWidth*3+x*3+1];
		newbits[y*bm.bmWidth*3+x*3+2]=bits[(bm.bmHeight-y-1)*bm.bmWidth*3+x*3];

	}
	int success=gluBuild2DMipmaps(GL_TEXTURE_2D,3,bm.bmWidth,bm.bmHeight,GL_RGB,GL_UNSIGNED_BYTE,newbits);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
	DeleteObject(himage);
	delete []newbits;
#ifndef PLMODEL
	char consolemessage[5000];
	strcpy(consolemessage,"Loaded ");
	strcat(consolemessage,filename);
	ConsoleDisplayText(consolemessage);
#endif
	return 1;
}

int LoadRaw(char *filename,UINT num)
{

	HANDLE file=CreateFile(filename,GENERIC_READ,0,0,OPEN_EXISTING,0,0);
	if(!file) return 0;
	UINT filelen=GetFileSize(file,0);
	if(filelen==-1) return 0;
	char *bits=new char[filelen];
	DWORD read=0;
	ReadFile(file,bits,1024,&read,NULL);
	filelen-=1024;
	ReadFile(file,bits,filelen,&read,NULL);
	CloseHandle(file);

	int channels=filelen;
	int size=1;
	do
	{
		channels/=4;
		size*=2;
	}while(channels>4);
	UINT type;
	if(channels==1) type=GL_LUMINANCE;
	if(channels==2) type=GL_LUMINANCE_ALPHA;
	if(channels==3) type=GL_RGB;
	if(channels==4) type=GL_RGBA;
	glBindTexture(GL_TEXTURE_2D,num);
	int success=gluBuild2DMipmaps(GL_TEXTURE_2D,channels,size,size,type,GL_UNSIGNED_BYTE,bits);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	delete []bits;		
#ifndef PLMODEL
	char consolemessage[5000];
	strcpy(consolemessage,"Loaded ");
	strcat(consolemessage,filename);
	ConsoleDisplayText(consolemessage);
	return 1;
#endif
}

void LoadBlockTextures()
{
	int filevalid=0;
	int tnum=0;
	int fnum=0;
	char tname[100];
	do
	{
		strcpy(tname,"textures\\block\\");
		strcat(tname,"wall");
		itoa(fnum,&tname[strlen(tname)],10);
		strcat(tname,".bmp");
		filevalid=LoadTexture(tname,400+tnum);
		tnum++;
		fnum++;
	}
	while(filevalid);
}
void LoadSpriteTextures()
{
	WIN32_FIND_DATA fd;
	HANDLE findhand=FindFirstFile("textures\\sprite\\*.bmp",&fd);
	int curtex=30;
	if(findhand==INVALID_HANDLE_VALUE) return;
	int found;
	reflecttexnum=-1;
	do
	{
		if(fd.dwFileAttributes!=FILE_ATTRIBUTE_DIRECTORY)
		{
			
			char fullfilename[300]="textures\\sprite\\";
			strcpy(texname[curtex-30],fd.cFileName);
			strcat(fullfilename,fd.cFileName);
			char rawfullfilename[300];
			strcpy(rawfullfilename,fullfilename);
			int i;
			for(i=strlen(rawfullfilename);i>=0;i--)
			{
				if(rawfullfilename[i]=='.')
					break;
			}
			if(i>0)
				strcpy(&rawfullfilename[i+1],"pxr");
			if(!LoadRaw(rawfullfilename,curtex))
				LoadTexture(fullfilename,curtex);
			int len=strlen(fd.cFileName);
			texused[curtex-30]=0;
			curtex++;
		}
		found=FindNextFile(findhand,&fd);
	}
	while (found);
	numtex=curtex-30;
	
}
int GetTexID(LPCTSTR name)
{
	if(stricmp("reflect.bmp",name)==0)
		return 1;
	for(int i=0;i<numtex;i++)
	{
		if(stricmp(name,texname[i])==0)
		{
			texused[i]=1;
			return i+30;
				
		}
	}
	return 0;
}

#ifndef PLMODEL

//selects a random texture from a directory
void GetRandom(LPCTSTR dir,char name[255])
{
	WIN32_FIND_DATA fd;
	char wildcard[300];
	strcpy(wildcard,dir);
	strcat(wildcard,"*.bmp");
	HANDLE findhand=FindFirstFile(wildcard,&fd);
	
	if(findhand==INVALID_HANDLE_VALUE) return;
	int found;
	int num=0;
	do
	{
		found=FindNextFile(findhand,&fd);
		num++;
	}
	while (found);
	int pick=num*RND;
	
	findhand=FindFirstFile(wildcard,&fd);
	
	if(findhand==INVALID_HANDLE_VALUE) return;
	
	num=0;
	do
	{
		if(num==pick)
		{
			strcpy(name,fd.cFileName);
			return;
		}
		found=FindNextFile(findhand,&fd);
		num++;
	}
	while (found);
}
void LoadRandom(LPCTSTR dir,int texnum)
{
	char fullfilename[400];
	char filename[255];
	GetRandom(dir,filename);
	strcpy(fullfilename,dir);
	strcat(fullfilename,filename);
	LoadTexture(fullfilename,texnum);
}
void LoadGuns()
{
	CfgFile objcfg;
	objcfg.Load("object.ini");
	char fullfilename[300];
	char val[300];
	char *wp;
	int numguns=objcfg.GetValuei("numguns");
	for(int i=0;i<numguns;i++)
	{
		strcpy(val,"gun");
		itoa(i,&val[3],10);
		wp=&val[strlen(val)];
		strcpy(wp,"main");
		strcpy(fullfilename,"objects\\");
		strcat(fullfilename,objcfg.GetValues(val));
		gun[i].main.Load(fullfilename);
		strcpy(wp,"part0");
		strcpy(fullfilename,"objects\\");
		const char *sr=objcfg.GetValues(val);
		if(sr)
		{
			strcat(fullfilename,sr);
			gun[i].part[0].Load(fullfilename);
		}
		strcpy(wp,"part1");
		strcpy(fullfilename,"objects\\");
		sr=objcfg.GetValues(val);
		if(sr)
		{
			strcat(fullfilename,sr);
			gun[i].part[1].Load(fullfilename);
		}
		strcpy(wp,"playermodel");
		strcpy(fullfilename,"objects\\");
		sr=objcfg.GetValues(val);
		if(sr)
		{
			strcat(fullfilename,sr);
			gun[i].playermodel.Load(fullfilename);
		}
		strcpy(wp,"explosion0");
		strcpy(fullfilename,"textures\\explosions\\");
		sr=objcfg.GetValues(val);
		if(sr)
		{
			strcat(fullfilename,sr);
			LoadRaw(fullfilename,100+i*2);
		}
		strcpy(wp,"explosionside0");
		strcpy(fullfilename,"textures\\explosions\\");
		sr=objcfg.GetValues(val);
		if(sr)
		{
			strcat(fullfilename,sr);
			LoadRaw(fullfilename,101+i*2);
		}
		
		strcpy(wp,"offset");
		gun[i].offset=objcfg.GetValuev(val)/2;
		strcpy(wp,"explosionoffset");
		gun[i].explosionoffset=objcfg.GetValuev(val)/2;
		strcpy(wp,"explosionsize");
		gun[i].explosionsize=objcfg.GetValuef(val)/2;
		strcpy(wp,"partoffset0");
		gun[i].partoffset[0]=objcfg.GetValuev(val)/2;
		strcpy(wp,"partoffset1");
		gun[i].partoffset[1]=objcfg.GetValuev(val)/2;
		strcpy(wp,"partup0");
		gun[i].partup[0]=objcfg.GetValuev(val);
		strcpy(wp,"partup1");
		gun[i].partup[1]=objcfg.GetValuev(val);
		strcpy(wp,"partright0");
		gun[i].partright[0]=objcfg.GetValuev(val);
		strcpy(wp,"partright1");
		gun[i].partright[1]=objcfg.GetValuev(val);
		gun[i].partup[0]=Normalize(gun[i].partup[0]);
		gun[i].partup[1]=Normalize(gun[i].partup[1]);
		gun[i].partright[0]=GetRight(gun[i].partup[0],gun[i].partright[0]);
		gun[i].partright[1]=GetRight(gun[i].partup[1],gun[i].partright[1]);
		strcpy(wp,"explosionnorotate");
		gun[i].explosionnorotate=objcfg.GetValuei(val);
		
	}
	
	torso[0][0].Load("objects\\torso_red_gat.cob");
	torso[0][1].Load("objects\\torso_red_m16.cob");
	torso[0][2].Load("objects\\torso_red_acr.cob");
	torso[0][3].Load("objects\\torso_red_ump.cob");
	torso[0][4].Load("objects\\torso_red_nade.cob");
	torso[0][5].Load("objects\\torso_red_beretta.cob");
	torso[0][6].Load("objects\\torso_red_whiterifle.cob");

	torso[1][0].Load("objects\\torso_blue_gat.cob");
	torso[1][1].Load("objects\\torso_blue_m16.cob");
	torso[1][2].Load("objects\\torso_blue_acr.cob");
	torso[1][3].Load("objects\\torso_blue_ump.cob");
	torso[1][4].Load("objects\\torso_blue_nade.cob");
	torso[1][5].Load("objects\\torso_blue_beretta.cob");
	torso[1][6].Load("objects\\torso_blue_whiterifle.cob");

	leg[0].Load("objects\\run_rightback.cob");
	leg[1].Load("objects\\run_rightback2.cob");
	leg[2].Load("objects\\run_neutral.cob");
	leg[3].Load("objects\\run_leftback2.cob");
	leg[4].Load("objects\\run_leftback.cob");
	LoadScenery();
}



void LoadScenery()
{
	CfgFile objcfg;
	objcfg.Load("object.ini");
	char prop[50];
	char *filename;
	char fullfilename[200];
	numscenery=0;
	do
	{
		strcpy(prop,"scenery");
		itoa(numscenery,&prop[strlen(prop)],10);
		filename=objcfg.GetValues(prop);
		if(filename)
		{
			strcpy(fullfilename,"objects\\");
			strcat(fullfilename,filename);
			scenery[numscenery].Load(fullfilename);
			numscenery++;
		}
	} while(filename);
	
	plobj[0].Load("objects\\best.cob");
	projectile[0].Load("objects\\rocket.cob");
	projectile[1].Load("objects\\roundz.cob");
	projectile[2].Load("objects\\nade.cob");
	servercobs[1][0].Load("objects\\dome.cob");
	servercobs[2][0].Load("objects\\waterpump.cob");
	servercobs[2][1].Load("objects\\station_red.cob");
	servercobs[2][2].Load("objects\\station_blue.cob");
	servercobs[3][0].Load("objects\\flag_blue.cob");
	servercobs[3][1].Load("objects\\flag_red.cob");
	servercobs[3][2].Load("objects\\flag_blue.cob");
	hangglider.Load("objects\\hangglider.cob");

}
int GetNumScenery()
{
	return numscenery;
}
void MoveUnnecessaryTextures()
{
	ConsoleDisplayText("Moving unnecessary textures");
	int numdisplayed=0;
	for(int i=0;i<numtex;i++)
	{
		if(!texused[i])
		{
			char oldfilename[5000],newfilename[5000];
			strcpy(oldfilename,"textures\\sprite\\");
			strcat(oldfilename,texname[i]);
			strcpy(newfilename,"textures\\sprite\\unused\\");
			strcat(newfilename,texname[i]);
			char message[10000];
			sprintf(message,"Moving %s to %s",oldfilename,newfilename);
			ConsoleDisplayText(message);
			MoveFileEx(oldfilename,newfilename,MOVEFILE_REPLACE_EXISTING);
		}
	}
}

void LoadHighTextures()
{
	LoadSpriteTextures();
	LoadBlockTextures();
	LoadGuns();
	
	LoadTexture("textures\\mainmenu.pxr",1300);
	int numtexused=0;
	for(int i=0;i<numtex;i++)
	{
		numtexused+=texused[i];
	}
	char consolemessage[1000];

	sprintf(consolemessage,"Sprite textures: %i Used: %i Percent: %i",numtex,numtexused,(int)((float)numtexused/numtex*100));
	ConsoleDisplayText(consolemessage);	

	
}
void LoadTextures()
{
	glEnable(GL_TEXTURE_2D);
	LoadTexture("textures\\ammoicon.bmp",10);
	LoadTexture("textures\\healthicon.bmp",11);
	
	LoadTexture("textures\\buildicon.bmp",20);
	LoadTexture("textures\\stationmoveicon.bmp",21);


	LoadTexture("textures\\sun.bmp",3);
	LoadTexture("textures\\sunback.bmp",4);
	LoadTexture("textures\\crosshair.bmp",5);
	LoadTexture("textures\\water\\seamlesswater.bmp",7);
	LoadTexture("textures\\lillypad.pxr",600);
	LoadTexture("textures\\gunnames.pxr",15);
}
#endif