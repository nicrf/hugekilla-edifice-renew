#ifndef FILEIO_H
#define FILEIO_H

//Portable file access (M12.4): std::filesystem for listing and folders, stdio for
//reading and writing, in place of the Win32 CreateFile/FindFirstFile shims.
//Paths are UTF-8 with '/' separators, relative to the data folder (the working
//directory) or absolute (UserFile). The data was made on Windows, where file names
//ignore case ("Structures" on disk, "structures" in the code; "wall3.BMP"): outside
//Windows, a path that does not exist as written is matched one component at a time,
//case-insensitively, against the folder listings (first match wins).

#include <stdio.h>
#include <string>
#include <vector>

std::string HKResolvePath(const char *path);          //host path (UTF-8), case fixed as above
FILE *HKFileOpen(const char *path, const char *mode);  //fopen, UTF-8 path also on Windows
char *HKReadFile(const char *path, long *size);        //whole file plus a NUL (delete[]), 0 if unreadable
int HKFileExists(const char *path);                    //1 if a file or folder is there
int HKMakeDir(const char *path);                       //1 if the folder exists or was created
int HKRenameFile(const char *from, const char *to);    //1 if moved; replaces the destination

struct HKDirEntry
{
	std::string name;
	int isdir;
};
//entries of a folder in the order the system lists them (as FindFirstFile and
//readdir did: texture slots depend on it), without "." and ".."; empty if missing
std::vector<HKDirEntry> HKListDir(const char *dir);
int HKHasExt(const char *name, const char *ext);       //1 if name ends with ext, any case (".bmp")

#endif
