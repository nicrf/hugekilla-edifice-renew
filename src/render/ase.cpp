//AseObj is a class that can load .cob (truespace) objects, including textures.  It also includes the functions to draw them

#include "StdAfx.h"
#include "vector.h"
#include "ase.h"
#include "redrawing.h"
#include "land.h"
#include "playing.h"
extern ServerVars sv;

void AseObj::Release()
{
	object.nummats=0;
	for(int curobj=0;curobj<object.numobjs;curobj++)
		{
			delete [](object.objs[curobj].pnt);

			delete [](object.objs[curobj].faces);
			
		}
	if(object.objs) delete [](object.objs);
	object.objs=0;
	object.numobjs=0;
	loaded=0;
}
AseObj::~AseObj()
{
	Release();
}
#define CHUNKLEN 20
#define F_HOLE 0x08
struct chunkhdr
{
	char type[4];
	short majver;
	short minver;
	int32_t id;
	int32_t parent;
	int32_t len;
};

chunkhdr ReadChunkHeader(char *buf,int bufpos,int buflen)
{
	chunkhdr a;
	if(bufpos<buflen-4)
		CopyMemory(&a.type,&buf[bufpos],4);
	bufpos+=4;
	if(bufpos<buflen-2)
		CopyMemory(&a.majver,&buf[bufpos],2);
	bufpos+=2;
	if(bufpos<buflen-2)
		CopyMemory(&a.minver,&buf[bufpos],2);
	bufpos+=2;
	if(bufpos<buflen-4)
		CopyMemory(&a.id,&buf[bufpos],4);
	bufpos+=4;
	if(bufpos<buflen-4)
		CopyMemory(&a.parent,&buf[bufpos],4);
	bufpos+=4;
	if(bufpos<buflen-4)
		CopyMemory(&a.len,&buf[bufpos],4);
	bufpos+=4;
	
	return a;
}
int ReadName(char *buf,char *&name)
{
	short len;
	CopyMemory(&len,buf,2);
	buf+=2;
	name=new char[len];
	CopyMemory(name,buf,len);
	return 2+len;
}

int ReadDupName(char *buf,short &dupecount,char *&name)
{
	CopyMemory(&dupecount,buf,2);
	buf+=2;
	int len=ReadName(buf,name);
	buf+=len;
	return 2+len;
}
int ReadVertices(char *buf,sobject *obj)
{
	int numpoints;
	CopyMemory(&numpoints,buf,4);
	buf+=4;
	obj->numpoints=numpoints;
	obj->pnt=new spoint[numpoints];
	for(int i=0;i<numpoints;i++)
	{
		CopyMemory(&obj->pnt[i].vctr.x,buf,4);
		buf+=4;
		CopyMemory(&obj->pnt[i].vctr.y,buf,4);
		buf+=4;
		CopyMemory(&obj->pnt[i].vctr.z,buf,4);
		buf+=4;
		
	}
	return 4+12*numpoints;

}
void TranslateVertices(sobject *obj,Vector center)
{
	
	for(int i=0;i<obj->numpoints;i++)
	{
		obj->pnt[i].vctr+=center;
	}

}
void TranslateVertices(sobject *obj,Vector x,Vector y,Vector z)
{
	for(int i=0;i<obj->numpoints;i++)
	{
		obj->pnt[i].vctr=obj->pnt[i].vctr.x*x+
		obj->pnt[i].vctr.y*y+
		obj->pnt[i].vctr.z*z;
	}
}

void TranslateVect(float mx[][4],Vector &p)
{
	Vector d;
	d.x=mx[0][0]*p.x+mx[0][1]*p.y+mx[0][2]*p.z+mx[0][3];
	d.y=mx[1][0]*p.x+mx[1][1]*p.y+mx[1][2]*p.z+mx[1][3];
	d.z=mx[2][0]*p.x+mx[2][1]*p.y+mx[2][2]*p.z+mx[2][3];
	p=d;
}

void TranslateVertices(sobject *obj,float mx[][4])
{
	for(int i=0;i<obj->numpoints;i++)
	{
		TranslateVect(mx,obj->pnt[i].vctr);
	}
}

int ReadUVVertices(char *buf,sobject *obj)
{
	int numtex;
	CopyMemory(&numtex,buf,4);
	buf+=4;
	obj->numtex=numtex;
	obj->tex=new uv[numtex];
	for(int i=0;i<numtex;i++)
	{
		CopyMemory(&obj->tex[i],buf,8);
		buf+=8;
		obj->tex[i].u*=-1;
		obj->tex[i].v*=-1;
	}
	return 4+8*numtex;
}

int ReadFaces(char *buf,sobject *obj)
{
	char *obuf=buf;
	int numface;
	CopyMemory(&numface,buf,4);
	buf+=4;
	obj->faces=new face[numface];
	obj->numfaces=numface;
	char flag;
	short numvertex;
	for(int i=0;i<numface;i++)
	{
		face *cur=&obj->faces[i];
		CopyMemory(&flag,buf,1);
		buf+=1;
		CopyMemory(&numvertex,buf,2);
		buf+=2;
		if(flag&&F_HOLE)
		{
			short mat;
			CopyMemory(&mat,buf,2);
			buf+=2;
			cur->otexture=mat;
			for(int i2=0;i2<numvertex;i2++)
			{
				int pref,tref;
				CopyMemory(&pref,buf,4);
				buf+=4;
				CopyMemory(&tref,buf,4);
				buf+=4;
				if(i2<3)
				{
					cur->pnt[i2]=&obj->pnt[pref];
					cur->tex[i2]=obj->tex[0];
					
				}
			}
			
		}
		else
		{
			short mat;
			CopyMemory(&mat,buf,2);
			buf+=2; //for mat
			cur->otexture=mat;
			for(int i3=0;i3<numvertex;i3++)
			{
				int i2=i3;
				//if(i2<2)i2=1-i2;
				int pref,tref;
				CopyMemory(&pref,buf,4);
				buf+=4;
				CopyMemory(&tref,buf,4);
				buf+=4;
				if(i2<3)
				{
					cur->pnt[i2]=&obj->pnt[pref];
					cur->tex[i2]=obj->tex[tref];
					
				}
			}

		}

	}
	return buf-obuf;


}
void ComputeNormals(sobject *obj)
{

	int i;
	for(i=0;i<obj->numfaces;i++)
	{
		obj->faces[i].normal=Normalize(CrossProduct(obj->faces[i].pnt[1]->vctr-obj->faces[i].pnt[0]->vctr,obj->faces[i].pnt[2]->vctr-obj->faces[i].pnt[0]->vctr));
	}
	for(i=0;i<obj->numpoints;i++)
	{
		Vector avg;
		avg.x=0;
		avg.y=0;
		avg.z=0;
		int num=0;
		for(int i2=0;i2<obj->numfaces;i2++)
		{
			if(&obj->pnt[i]==obj->faces[i2].pnt[0]||
				&obj->pnt[i]==obj->faces[i2].pnt[1]||
				&obj->pnt[i]==obj->faces[i2].pnt[2])
			{
				avg+=obj->faces[i2].normal;
				num++;
			}
		}
		if(num) obj->pnt[i].nvctr=Normalize(avg/num);
	}
}
void ReadObject(char *buf,int buflen,sobject *obj)
{
	char *max=&buf[buflen];
	short dupecount;
	char *name;
	
	buf+=ReadDupName(buf,dupecount,name);
	Vector center,x,y,z;
	CopyMemory(&center,buf,12);
	buf+=12;
	CopyMemory(&x,buf,12);
	buf+=12;
	CopyMemory(&z,buf,12);
	buf+=12;
	CopyMemory(&y,buf,12);
	buf+=12;
	float mx[4][4];
	CopyMemory(mx,buf,4*4*3);
	buf+=16*3;
	mx[3][0]=0;
	mx[3][1]=0;
	mx[3][2]=0;
	mx[3][3]=1;

	buf+=ReadVertices(buf,obj);
	
	TranslateVertices(obj,mx);	

	buf+=ReadUVVertices(buf,obj);
	buf+=ReadFaces(buf,obj);
	
	float tmp;
	for(int i=0;i<obj->numpoints;i++)
	{
		tmp=obj->pnt[i].vctr.y;
		obj->pnt[i].vctr.y=obj->pnt[i].vctr.z;
		obj->pnt[i].vctr.z=tmp;
		obj->pnt[i].vctr.x*=-1;
	}
	ComputeNormals(obj);



}
void ReadMat(char *buf,int buflen,texinfo *tx)
{
	short id;
	CopyMemory(&id,buf,2);
	tx->id=id;
	buf+=2;
	buf+=3; //shader,facet,facet angle
	float red;
	CopyMemory(&red,buf,4);
	buf+=4;
	float green;
	CopyMemory(&green,buf,4);
	buf+=4;
	float blue;
	CopyMemory(&blue,buf,4);
	buf+=4;
	float alpha;
	CopyMemory(&alpha,buf,4);
	buf+=4;
	float ambient;
	CopyMemory(&ambient,buf,4);
	buf+=4;
	float specular;
	CopyMemory(&specular,buf,4);
	buf+=4;
	float highlight;
	CopyMemory(&highlight,buf,4);
	buf+=4;
	float idxofrefract;
	CopyMemory(&idxofrefract,buf,4);
	buf+=4;
	if(buf[0]=='e'&&buf[1]==':')
	{
		buf+=3;
		char *name;
		buf+=ReadName(buf,name);
	}
	
	if(buf[0]=='t'&&buf[1]==':')
	{
		buf+=3;
		char *name;
		int len=ReadName(buf,name);
		buf+=len;
		len-=2;
		char filename[300];
		int filenamelen=0;
		for(int i=len-1;i>=0;i--)
		{
			if(name[i]=='\\') break;
			MoveMemory(&filename[1],filename,299);
			filename[0]=name[i];
			filenamelen++;
		}
		filename[filenamelen]='\0';
		float uoffset,voffset,urepeat,vrepeat;
		CopyMemory(&uoffset,buf,4);
		buf+=4;
		CopyMemory(&voffset,buf,4);
		buf+=4;
		CopyMemory(&urepeat,buf,4);
		buf+=4;
		CopyMemory(&vrepeat,buf,4);
		buf+=4;
		tx->offset.u=-uoffset;
		tx->offset.v=-voffset;
		tx->repeat.u=-urepeat;
		tx->repeat.v=vrepeat;
		tx->texid=GetTexID(filename);

		delete []name;
		
	}
}
int AseObj::LoadCob(char *buf,int buflen)
{
	int numobjs=0,nummats=0;
	int bufpos=32;
	do
	{
		chunkhdr hdr=ReadChunkHeader(buf,bufpos,buflen);
		if(strncmp("END",hdr.type,3)==0) break;
		if(strncmp("PolH",hdr.type,4)==0) numobjs++;
		if(strncmp("Mat1",hdr.type,4)==0) nummats++;
		bufpos+=CHUNKLEN;
		bufpos+=hdr.len;
	} while (bufpos<buflen);
	object.nummats=nummats;
	texinfo *tex=new texinfo[nummats];
	object.numobjs=numobjs;
	object.objs=new sobject[numobjs];
	
	nummats=0;
	numobjs=0;
	bufpos=32;

	do
	{
		chunkhdr hdr=ReadChunkHeader(buf,bufpos,buflen);
		if(strncmp("END",hdr.type,3)==0) break;
		bufpos+=CHUNKLEN;
		if(strncmp("PolH",hdr.type,4)==0) 
		{
			object.objs[numobjs].id=hdr.id;
			ReadObject(&buf[bufpos],hdr.len,&object.objs[numobjs++]);
		}
		if(strncmp("Mat1",hdr.type,4)==0) 
		{
			int i;
			for(i=0;i<object.numobjs;i++)
			{
				if(object.objs[i].id==hdr.parent)
					break;
			}
			ReadMat(&buf[bufpos],hdr.len,&tex[nummats]);
			if(i!=object.numobjs)
			{
				for(int i2=0;i2<object.objs[i].numfaces;i2++)
				{
					if(object.objs[i].faces[i2].otexture==tex[nummats].id)
					{
						object.objs[i].faces[i2].texture=tex[nummats].texid;
						texinfo *tx=&tex[nummats];
						for(int side=0;side<3;side++)
						{
							object.objs[i].faces[i2].tex[side].u=object.objs[i].faces[i2].tex[side].u*tx->repeat.u+tx->offset.u;
							object.objs[i].faces[i2].tex[side].v=object.objs[i].faces[i2].tex[side].v*tx->repeat.v+tx->offset.v;
						}			

					}
				}
			}
			nummats++;
		}
		bufpos+=hdr.len;
	} while (bufpos<buflen);
	for(int i=0;i<object.numobjs;i++)
	{
		
		sobject *obj=&object.objs[i];
		obj->lastcompx=Vector(0,0,0);
		texinfo *tx=&tex[i];
	}	
	delete []tex;
	
	return 1;

}

int AseObj::Load(LPCTSTR fil)
{
#ifdef HKDS
	return 0;
#endif
	Release();
	if(object.numobjs) return 0;
	HANDLE file=CreateFile(fil,GENERIC_READ,0,0,OPEN_EXISTING,0,0);
	if(!file) return 0;
	int filelen=GetFileSize(file,0);
	if(filelen==-1) return 0;
	char *buf=new char[filelen];
	DWORD read=0;
	ReadFile(file,buf,filelen,&read,NULL);
	CloseHandle(file);
	if(strncmp(buf,"Caligari",8)==0)
	{
		return loaded=LoadCob(buf,filelen);
	}
	delete []buf;
	loaded=1;
	return 1;
}

void AseObj::Unload()
{
}
int totalfaces;
int facesthissec;
void AseObj::Draw(Vector pos,Vector x,Vector y)
{
	Vector z=CrossProduct(x,y);
	float m[4][4]={
	{x.x*sv.scale,x.y*sv.scale,x.z*sv.scale,0}, 
	{y.x*sv.scale,y.y*sv.scale,y.z*sv.scale,0}, 
	{z.x*sv.scale,z.y*sv.scale,z.z*sv.scale,0}, 
	{pos.x,pos.y,pos.z,1}
	};
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glMultMatrixf((float *)&m);

	glBegin(GL_TRIANGLES);
	int curtex=0;
	Vector lightdir;
	lightdir.x=0;
	lightdir.y=GetSkySettings()->sunheight;
	lightdir.z=1;
	lightdir=Normalize(lightdir);
	face *curface;
	Vector *curvect;
	Vector translatedvect;
	spoint *curpnt;
	float dot;
	for(int curobj=0;curobj<object.numobjs;curobj++)
	{
		if(!(object.objs[curobj].lastcompx.x==x.x&&
			object.objs[curobj].lastcompx.y==x.y&&
			object.objs[curobj].lastcompx.z==x.z&&
			object.objs[curobj].lastcompy.x==y.x&&
			object.objs[curobj].lastcompy.y==y.y&&
			object.objs[curobj].lastcompy.z==y.z))
		for(int point=0;point<object.objs[curobj].numpoints;point++)
		{
			curpnt=&object.objs[curobj].pnt[point];
			translatedvect=VectMult(curpnt->nvctr,x,y,z);
			dot=(-DotProduct(lightdir,translatedvect));
			curpnt->curcolor=dot/1.4+.2;
			if(curpnt->curcolor<.2)curpnt->curcolor=.2;
			if(dot>.8) curpnt->curcolor*=(dot+.2)*1.1f;
		}
		object.objs[curobj].lastcompx=x;
		object.objs[curobj].lastcompy=y;
		for(int face=0;face<object.objs[curobj].numfaces;face++)
		{
			curface=&object.objs[curobj].faces[face];
			if(curface->texture!=curtex)
			{

				glEnd();
				if(curface->texture<5)
					TexBind(TEX_GROUND0);
				else
					TexBind(curface->texture);
				
				curtex=curface->texture;
				glBegin(GL_TRIANGLES);
			}
			float color;
			
			for(int point=0;point<3;point++)
			{
				totalfaces+=1;
				curvect=&curface->pnt[point]->vctr;
				glColor4f(curface->pnt[point]->curcolor,
				curface->pnt[point]->curcolor,
				curface->pnt[point]->curcolor,1);
	
				if(curtex>5)
				{
					glTexCoord2f(curface->tex[point].u,curface->tex[point].v);
				}
				else
				{
					glColor3f(.5,.5,1);
					translatedvect=VectMult(curface->pnt[point]->nvctr,x,y,z);
					glTexCoord2f(atan(translatedvect.x)/5,translatedvect.y/5);
				}
				glVertex3fv(&curvect->x);
					
			}
		}
		
	}
	
	glEnd();
	glPopMatrix();
}
