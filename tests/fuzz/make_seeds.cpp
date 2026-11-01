// Writes seed inputs for the harnesses of tests/fuzz into DIR/<harness>/:
// valid messages made with the game's own writers (netmsg.h, sock.cpp,
// voicemsg.h, mapfile.cpp) and datagrams framed like Sock sends them.
// The committed corpora (tests/fuzz/corpus) come from this program, plus
// messages captured from a real server (tests/net_join.py, NET_JOIN_DUMP).
//   make_seeds tests/fuzz/corpus
#include "fuzz.h"
#include "sock.h"
#include "mapfile.h"
#include "gameplay.h"   //DOME, FLAG
#include <sys/stat.h>
#include <string>
#include <vector>

uint32_t HKTicks() { return 0; }   //system_sdl.cpp isn't linked: a fixed clock

static std::string root;

static void Seed(const char *harness,const char *name,const void *data,size_t size)
{
	std::string dir=root+"/"+harness;
	mkdir(dir.c_str(),0755);
	std::string path=dir+"/"+name;
	FILE *f=fopen(path.c_str(),"wb");
	if(!f||fwrite(data,1,size,f)!=size) { fprintf(stderr,"can't write %s\n",path.c_str()); exit(1); }
	fclose(f);
}

typedef std::vector<unsigned char> Bytes;

//a datagram of grouped messages, as Sock::Send/Flush make it
static Bytes Grouped(const std::vector<Bytes> &msgs)
{
	Bytes d(1,0);
	for(size_t i=0;i<msgs.size();i++)
	{
		d.push_back(msgs[i].size()&0xff);
		d.push_back(msgs[i].size()>>8);
		d.insert(d.end(),msgs[i].begin(),msgs[i].end());
	}
	return d;
}

//one reliable transfer of messages (uint32 size + data each), cut in parts of 450 bytes
static std::vector<Bytes> Reliable(int id,const std::vector<Bytes> &msgs)
{
	Bytes all;
	for(size_t i=0;i<msgs.size();i++)
	{
		unsigned char n[4];
		WirePutU32(n,(uint32_t)msgs[i].size());
		all.insert(all.end(),n,n+4);
		all.insert(all.end(),msgs[i].begin(),msgs[i].end());
	}
	std::vector<Bytes> parts;
	for(size_t pos=0,num=0;pos<all.size();num++)
	{
		size_t chunk=all.size()-pos<450?all.size()-pos:450;
		Bytes body(4);
		WirePutU16(&body[0],id);
		WirePutU16(&body[2],(uint32_t)num);
		body.insert(body.end(),all.begin()+pos,all.begin()+pos+chunk);
		pos+=chunk;
		unsigned char sum[4]={0,0,0,0};
		for(size_t i=0;i<body.size();i++) sum[i&3]^=body[i];
		Bytes d(1,(unsigned char)(pos>=all.size()?-2:-1));   //LASTRELIABLE, RELIABLE
		d.insert(d.end(),sum,sum+4);
		d.insert(d.end(),body.begin(),body.end());
		parts.push_back(d);
	}
	return parts;
}

//fuzz_sock input: options, then uint16 length + datagram
static Bytes SockInput(int options,const std::vector<Bytes> &datagrams)
{
	Bytes in(1,(unsigned char)options);
	for(size_t i=0;i<datagrams.size();i++)
	{
		in.push_back(datagrams[i].size()&0xff);
		in.push_back(datagrams[i].size()>>8);
		in.insert(in.end(),datagrams[i].begin(),datagrams[i].end());
	}
	return in;
}

template<class T,int (*W)(void *,int,T *)> static Bytes Msg(T &m)
{
	static unsigned char buf[BIGBUFSIZE];
	int n=W(buf,sizeof(buf),&m);
	if(n<=0) { fprintf(stderr,"writer failed\n"); exit(1); }
	return Bytes(buf,buf+n);
}

int main(int argc,char **argv)
{
	if(argc<2) { fprintf(stderr,"usage: make_seeds DIR\n"); return 1; }
	root=argv[1];
	mkdir(root.c_str(),0755);

	PlayerPosition pos;
	memset(&pos,0,sizeof(pos));
	pos.type=PLAYERPOSITION; pos.pnum=1; pos.weapon=2; pos.state=1; pos.fwd=200; pos.tilt=30;
	pos.posx=12000; pos.posy=3000; pos.posz=40000; pos.velx=-3; pos.vely=1; pos.velz=5;
	Bytes position=Msg<PlayerPosition,WritePosition>(pos);
	Seed("fuzz_position","position.bin",position.data(),position.size());

	FireGunDirection fg;
	memset(&fg,0,sizeof(fg));
	fg.type=FIREGUN; fg.pnum=2; fg.reload=1;
	fg.dir=Vector(0,0,1); fg.pos=Vector(100,30,200);
	Bytes firegun=Msg<FireGunDirection,WriteFireGun>(fg);
	Seed("fuzz_firegun","firegun.bin",firegun.data(),firegun.size());

	KilledPosition kp;
	memset(&kp,0,sizeof(kp));
	kp.type=KILLEDPOSITION; kp.killed=1; kp.killer=0; kp.pos=Vector(5,6,7);
	Bytes killed=Msg<KilledPosition,WriteKilledPosition>(kp);
	Seed("fuzz_killedposition","killed.bin",killed.data(),killed.size());

	UpdateServerObject uso;
	memset(&uso,0,sizeof(uso));
	uso.type=UPDATESERVEROBJECT; uso.num=3;
	uso.updated.type=FLAG; uso.updated.pos=Vector(10,20,30); uso.updated.team=1; uso.updated.speedpenalty=.5f;
	Bytes update=Msg<UpdateServerObject,WriteUpdateServerObject>(uso);
	Seed("fuzz_updateserverobject","update.bin",update.data(),update.size());

	static ServerObjectSnapshot snap;
	snap.type=SERVEROBJECTSNAPSHOT;
	snap.obj[0]=uso.updated;
	snap.obj[1].type=DOME; snap.obj[1].pos=Vector(1,2,3);
	Bytes snapshot=Msg<ServerObjectSnapshot,WriteSnapshot>(snap);
	Seed("fuzz_snapshot","snapshot.bin",snapshot.data(),snapshot.size());

	ServerVarsMessage svm;
	memset(&svm,0,sizeof(svm));
	svm.type=SERVERVARS;
	svm.v.gravity=.01f; svm.v.walkaccelerate=.02f; svm.v.capturelimit=5; svm.v.timelimit=10;
	svm.v.positionresendtime=100; svm.v.scale=1;
	Bytes servervars=Msg<ServerVarsMessage,WriteServerVars>(svm);
	Seed("fuzz_servervars","servervars.bin",servervars.data(),servervars.size());

	RemoteGameDataMessage rgdm;
	rgdm.type=REMOTEGAMEDATA; rgdm.d.gamestate=2;
	Bytes rgd=Msg<RemoteGameDataMessage,WriteRGD>(rgdm);
	Seed("fuzz_rgd","rgd.bin",rgd.data(),rgd.size());

	static LandInfo li;
	li.type=LANDINFO;
	li.ldfs.numdefs=NUMDEF-1;
	for(int i=0;i<NUMDEF;i++)
	{
		li.ldfs.df[i].type=i%3; li.ldfs.df[i].sx=i*3; li.ldfs.df[i].sy=500-i*3;
		li.ldfs.df[i].sh=20; li.ldfs.df[i].sw=100; li.ldfs.df[i].sc=.3f; li.ldfs.df[i].sd=4; li.ldfs.df[i].sf=.5f;
	}
	li.ldfs.numgroundtex=2;
	strcpy(li.ldfs.groundtex[0],"grass.bmp");
	strcpy(li.ldfs.groundtex[1],"dirt.bmp");
	li.ldfs.randomseed=1234; li.ldfs.numscenery=10; li.ldfs.numblotch=150;
	li.skysets.sunheight=.6f; li.skysets.fogr=.5f;
	strcpy(li.skysets.cloudtex,"clouds.bmp");
	strcpy(li.skysets.sound,"wind.wav");
	Bytes landinfo=Msg<LandInfo,WriteLandInfo>(li);
	Seed("fuzz_landinfo","landinfo.bin",landinfo.data(),landinfo.size());

	Bytes simple(SIMPLESIZE);
	WriteSimple(&simple[0],PORTACK,0x1234,0);
	Seed("fuzz_simple","portack.bin",simple.data(),simple.size());
	Bytes ping(SIMPLESIZE);
	WriteSimple(&ping[0],PING,77,0);
	Seed("fuzz_simple","ping.bin",ping.data(),ping.size());

	//REQUESTENTER: type, n, name, NUL, version trailer
	Bytes hello;
	hello.push_back(REQUESTENTER);
	hello.push_back(6);
	const char *name="Joiner";
	hello.insert(hello.end(),name,name+6);
	hello.push_back(0);
	char trailer[VERSIONTRAILER_MAX];
	int tn=WriteVersionTrailer(trailer);
	hello.insert(hello.end(),trailer,trailer+tn);
	Seed("fuzz_hello","requestenter.bin",hello.data(),hello.size());
	Bytes oldhello(hello.begin(),hello.begin()+9);   //original game: no trailer
	Seed("fuzz_hello","original.bin",oldhello.data(),oldhello.size());
	//with capabilities and an echoed join cookie (M11 phase 2)
	char caps[HELLOCAPS_MAX];
	const unsigned char cookie[HELLOCOOKIE_SIZE]={1,2,3,4,5,6,7,8};
	int cn=WriteHelloCaps(caps,HELLOCAP_COOKIE,cookie);
	Bytes cookiehello(hello);
	cookiehello.insert(cookiehello.end(),caps,caps+cn);
	Seed("fuzz_hello","requestenter_cookie.bin",cookiehello.data(),cookiehello.size());

	//PLAYERNAMES with two players
	Bytes names;
	names.push_back(PLAYERNAMES);
	names.push_back(2);
	const char *pn[2]={"Host","Joiner"};
	for(int i=0;i<2;i++)
	{
		unsigned char e[11];
		e[0]=i;
		WirePutU32(e+1,3*i);
		WirePutU32(e+5,i);
		e[9]=i;
		e[10]=strlen(pn[i]);
		names.insert(names.end(),e,e+11);
		names.insert(names.end(),pn[i],pn[i]+strlen(pn[i]));
	}
	Seed("fuzz_playernames","playernames.bin",names.data(),names.size());

	//CHATMESSAGE
	const char *text="hello world";
	Bytes chat;
	chat.push_back(CHATMESSAGE);
	chat.push_back(CMSG_BROADCAST);
	chat.push_back(1);
	chat.push_back(strlen(text));
	chat.push_back(0);
	chat.insert(chat.end(),text,text+strlen(text));
	Seed("fuzz_chat","chat.bin",chat.data(),chat.size());

	//BLOCKS: a small wall and a deletion
	Bytes blocks(5);
	blocks[0]=BLOCKS;
	int nb=0;
	for(int i=0;i<6;i++,nb++)
	{
		unsigned char b[9]={(unsigned char)(10+i),5,(unsigned char)(250+i),1,0,2,3,4,0};
		if(i==5) b[8]=1;
		if(i==4) b[3]=4;
		blocks.insert(blocks.end(),b,b+9);
	}
	WirePutU32(&blocks[1],nb);
	Seed("fuzz_blocks","blocks.bin",blocks.data(),blocks.size());

	//VOICE
	VoiceMessage vm;
	vm.player=1; vm.seq=513; vm.len=60;
	for(int i=0;i<60;i++) vm.data[i]=i*7;
	unsigned char vbuf[VOICEMSGSIZE];
	int vn=WriteVoice(vbuf,sizeof(vbuf),&vm);
	Seed("fuzz_voice","voice.bin",vbuf,vn);

	//Sock: grouped messages; a reliable transfer in two parts; an ACK
	std::vector<Bytes> grouped;
	grouped.push_back(position);
	grouped.push_back(simple);
	grouped.push_back(Bytes(vbuf,vbuf+vn));
	std::vector<Bytes> d1(1,Grouped(grouped));
	Bytes in=SockInput(4,d1);
	Seed("fuzz_sock","grouped.bin",in.data(),in.size());
	std::vector<Bytes> transfer;
	transfer.push_back(names);
	transfer.push_back(landinfo);
	std::vector<Bytes> parts=Reliable(1,transfer);
	in=SockInput(6,parts);
	Seed("fuzz_sock","reliable.bin",in.data(),in.size());
	Bytes ack(5);
	ack[0]=(unsigned char)-3;   //ACK id 1 part 0
	WirePutU16(&ack[1],1);
	WirePutU16(&ack[3],0);
	in=SockInput(8|4,std::vector<Bytes>(1,ack));
	Seed("fuzz_sock","ack.bin",in.data(),in.size());
	printf("seeds written to %s\n",root.c_str());
	return 0;
}
