// Stand-in for libFuzzer's main when the compiler has no libFuzzer runtime
// (Apple clang): runs the harness once over each file given, or each file of
// each folder given. Arguments starting with '-' (libFuzzer options) are ignored.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size);

static int RunFile(const char *path)
{
	FILE *f=fopen(path,"rb");
	if(!f) { fprintf(stderr,"can't open %s\n",path); return 0; }
	fseek(f,0,SEEK_END);
	long size=ftell(f);
	fseek(f,0,SEEK_SET);
	uint8_t *data=(uint8_t *)malloc(size>0?size:1);   //exact size: ASan sees overreads
	size_t got=size>0?fread(data,1,size,f):0;
	fclose(f);
	LLVMFuzzerTestOneInput(data,got);
	free(data);
	return 1;
}

int main(int argc,char **argv)
{
	int runs=0;
	for(int i=1;i<argc;i++)
	{
		if(argv[i][0]=='-') continue;
		struct stat st;
		if(stat(argv[i],&st)) { fprintf(stderr,"no such file %s\n",argv[i]); return 1; }
		if(!S_ISDIR(st.st_mode)) { runs+=RunFile(argv[i]); continue; }
		DIR *d=opendir(argv[i]);
		while(struct dirent *e=d?readdir(d):0)
		{
			if(e->d_name[0]=='.') continue;
			char path[2000];
			snprintf(path,sizeof(path),"%s/%s",argv[i],e->d_name);
			if(!stat(path,&st)&&S_ISREG(st.st_mode)) runs+=RunFile(path);
		}
		if(d) closedir(d);
	}
	printf("%s: %d input(s) run without libFuzzer\n",argv[0],runs);
	return 0;
}
