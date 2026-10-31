
#ifndef ASE
#define ASE

struct spoint
{
	Vector vctr;
	Vector nvctr;
	float curcolor;
};
struct uv
{
	float u,v;
};
struct face
{
	spoint *pnt[3];
	Vector normal;
	uv tex[3];
	int texture;
	int otexture;
};
struct texinfo
{
	int id;
	int parent;
	int texid;
	uv offset;
	uv repeat;
};
	
struct sobject
{
	spoint *pnt;
	uv *tex;
	face *faces;	
	Vector lastcompx,lastcompy;
	int numpoints,numfaces,numtex;
	int id;
};
struct aseobj
{
	int nummats,numobjs;
	sobject *objs;
};
class AseObj
{
public:
	AseObj()
	{
		ZeroMemory(&object,sizeof(aseobj));
		loaded=0;
	}
	int Load(LPCTSTR file);
	int loaded;
	void Unload();
	void Draw(Vector pos,Vector top,Vector right);
	~AseObj();
	void Release();
	aseobj object;
private:
	
	int LoadCob(char *buf,int buflen);
};

#endif