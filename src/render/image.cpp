//image files to pixels with stb_image (M12.4), in place of the Win32 LoadImage
//(and its BMP-only shim); .pxr files stay with LoadRaw (textures.cpp)

#include "fileio.h"
#include "textures.h"

//BMP is all the data has; PNG and TGA cost little and take an alpha channel.
//Files are read through HKReadFile (UTF-8 paths, case lookup), so no stdio here.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_BMP
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb/stb_image.h"

unsigned char *LoadImagePixels(const char *filename,int *width,int *height,int channels)
{
	long size;
	char *data=HKReadFile(filename,&size);
	if(!data) return 0;
	int filechannels;
	unsigned char *pixels=stbi_load_from_memory((const stbi_uc *)data,(int)size,width,height,&filechannels,channels);
	delete []data;
	return pixels;
}

void FreeImagePixels(unsigned char *pixels)
{
	stbi_image_free(pixels);
}
