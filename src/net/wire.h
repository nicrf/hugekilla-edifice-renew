//explicit serialization of network messages (roadmap M6): every field is written
//and read byte by byte in little-endian, whatever the compiler's struct layout.
//ByteWriter/ByteReader never touch memory outside their buffer: a write that
//doesn't fit or a read past the end marks them invalid (Ok()==0), reads then
//return 0, and callers drop the message.
//
//The lower-case methods (u8, i32, f32, str...) take a reference, so that one
//template function can list the fields of a struct once for both directions:
//	template<class IO> void SyncFoo(IO &io,Foo &f) { io.i32(f.a); io.f32(f.b); }

#ifndef WIREH
#define WIREH

#include <string.h>
#include <stdint.h>

//raw little-endian access to a byte buffer whose size the caller has checked
inline void WirePutU16(void *p,uint32_t v)
{
	unsigned char *b=(unsigned char *)p;
	b[0]=v&0xff;
	b[1]=(v>>8)&0xff;
}
inline void WirePutU32(void *p,uint32_t v)
{
	unsigned char *b=(unsigned char *)p;
	b[0]=v&0xff;
	b[1]=(v>>8)&0xff;
	b[2]=(v>>16)&0xff;
	b[3]=(v>>24)&0xff;
}
inline uint32_t WireGetU16(const void *p)
{
	const unsigned char *b=(const unsigned char *)p;
	return b[0]|(b[1]<<8);
}
inline uint32_t WireGetU32(const void *p)
{
	const unsigned char *b=(const unsigned char *)p;
	return b[0]|(b[1]<<8)|(b[2]<<16)|((uint32_t)b[3]<<24);
}

class ByteWriter
{
public:
	ByteWriter(void *buf,int size) : p((unsigned char *)buf),cap(size>0?size:0),pos(0),ok(1) {}
	int Ok() const { return ok; }
	int Size() const { return pos; }

	void U8(uint32_t v) { if(Room(1)) p[pos++]=v&0xff; }
	void U16(uint32_t v) { if(Room(2)) { WirePutU16(&p[pos],v); pos+=2; } }
	void U32(uint32_t v) { if(Room(4)) { WirePutU32(&p[pos],v); pos+=4; } }
	void I32(int32_t v) { U32((uint32_t)v); }
	void F32(float v) { uint32_t u; memcpy(&u,&v,4); U32(u); }   //IEEE 754 bits
	void Bytes(const void *src,int n)
	{
		if(n<0) { ok=0; return; }
		if(Room(n)) { memcpy(&p[pos],src,n); pos+=n; }
	}
	//fixed-size string field: n bytes, the text then zeros (never what follows its NUL)
	void Str(const char *s,int n)
	{
		if(n<0||!Room(n)) { ok=0; return; }
		int len=0;
		while(len<n&&s[len]) len++;
		memcpy(&p[pos],s,len);
		memset(&p[pos+len],0,n-len);
		pos+=n;
	}

	//same, by reference (see the top of the file)
	void u8(const unsigned char &v) { U8(v); }
	void i8(const char &v) { U8((unsigned char)v); }
	void u16(const uint16_t &v) { U16(v); }
	void u32(const uint32_t &v) { U32(v); }
#ifdef _WIN32
	void u32(const unsigned long &v) { U32((uint32_t)v); }
#endif
	void i32(const int &v) { I32(v); }
	void f32(const float &v) { F32(v); }
	void str(const char *s,int n) { Str(s,n); }

private:
	int Room(int n)
	{
		if(!ok||n>cap-pos) { ok=0; return 0; }
		return 1;
	}
	unsigned char *p;
	int cap,pos,ok;
};

class ByteReader
{
public:
	ByteReader(const void *buf,int size) : p((const unsigned char *)buf),cap(size>0?size:0),pos(0),ok(1) {}
	int Ok() const { return ok; }
	int Pos() const { return pos; }
	int Left() const { return ok?cap-pos:0; }
	const unsigned char *Here() const { return p+pos; }

	uint32_t U8() { if(!Have(1)) return 0; return p[pos++]; }
	uint32_t U16() { if(!Have(2)) return 0; uint32_t v=WireGetU16(&p[pos]); pos+=2; return v; }
	uint32_t U32() { if(!Have(4)) return 0; uint32_t v=WireGetU32(&p[pos]); pos+=4; return v; }
	int32_t I32() { return (int32_t)U32(); }
	float F32() { uint32_t u=U32(); float v; memcpy(&v,&u,4); return v; }
	void Skip(int n) { if(n<0) ok=0; else if(Have(n)) pos+=n; }
	void Bytes(void *dst,int n)
	{
		if(n<0) ok=0;
		if(n>0&&Have(n)) { memcpy(dst,&p[pos],n); pos+=n; }
		else if(n>0) memset(dst,0,n);
	}
	//fixed-size string field of n bytes into dst[n], always NUL-terminated
	void Str(char *dst,int n)
	{
		if(n<=0) return;
		Bytes(dst,n);
		dst[n-1]=0;
	}

	//same, by reference (see the top of the file)
	void u8(unsigned char &v) { v=(unsigned char)U8(); }
	void i8(char &v) { v=(char)U8(); }
	void u16(uint16_t &v) { v=(uint16_t)U16(); }
	void u32(uint32_t &v) { v=U32(); }
#ifdef _WIN32
	void u32(unsigned long &v) { v=U32(); }   //DWORD of the real windows.h
#endif
	void i32(int &v) { v=I32(); }
	void f32(float &v) { v=F32(); }
	void str(char *s,int n) { Str(s,n); }

private:
	int Have(int n)
	{
		if(!ok||n>cap-pos) { ok=0; pos=cap; return 0; }
		return 1;
	}
	const unsigned char *p;
	int cap,pos,ok;
};

#endif
