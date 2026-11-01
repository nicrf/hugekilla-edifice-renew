//Portable file access, see fileio.h.

#include "fileio.h"
#include <filesystem>
#include <ctype.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

namespace fs = std::filesystem;

//UTF-8 to a host path: Windows would read a narrow string in the ANSI code page
static fs::path HostPath(const std::string &utf8)
{
#ifdef _WIN32
	return fs::u8path(utf8);
#else
	return fs::path(utf8);
#endif
}

#ifndef _WIN32
static int SameNoCase(const std::string &a, const char *b, size_t len)
{
	return a.size() == len && !strncasecmp(a.c_str(), b, len);
}

//the Windows-made data names its files in any case: when the path doesn't exist as
//written, each component takes the case of the first entry of its folder that
//matches it; the first component that matches nothing ends the search (new files)
static std::string FixCase(const std::string &in)
{
	std::error_code ec;
	if (in.empty() || fs::exists(in, ec)) return in;
	std::string out = in;
	size_t start = out[0] == '/' ? 1 : 0;
	while (start < out.size())
	{
		size_t end = out.find('/', start);
		size_t len = (end == std::string::npos ? out.size() : end) - start;
		const char *part = out.c_str() + start;
		int matched = (len == 1 && part[0] == '.') || (len == 2 && part[0] == '.' && part[1] == '.');
		std::string parent = start == 0 ? "." : start == 1 ? "/" : out.substr(0, start - 1);
		fs::directory_iterator it(parent, ec), last;
		for (; !matched && len && !ec && it != last; it.increment(ec))
		{
			std::string name = it->path().filename().string();
			if (SameNoCase(name, part, len))
			{
				out.replace(start, len, name);
				matched = 1;
			}
		}
		if (end == std::string::npos || !matched) break;
		start = end + 1;
	}
	return out;
}
#endif

std::string HKResolvePath(const char *path)
{
#ifdef _WIN32
	return path;               //the file system ignores case already
#else
	return FixCase(path);
#endif
}

FILE *HKFileOpen(const char *path, const char *mode)
{
	std::string p = HKResolvePath(path);
#ifdef _WIN32
	wchar_t wmode[8];
	size_t i = 0;
	for (; mode[i] && i < 7; i++) wmode[i] = (wchar_t)mode[i];
	wmode[i] = 0;
	return _wfopen(HostPath(p).c_str(), wmode);
#else
	return fopen(p.c_str(), mode);
#endif
}

char *HKReadFile(const char *path, long *size)
{
	if (size) *size = 0;
	FILE *f = HKFileOpen(path, "rb");
	if (!f) return 0;
	long len = -1;
	if (!fseek(f, 0, SEEK_END)) len = ftell(f);
	if (len < 0 || fseek(f, 0, SEEK_SET))
	{
		fclose(f);
		return 0;
	}
	char *data = new char[len + 1];
	len = (long)fread(data, 1, (size_t)len, f);
	fclose(f);
	data[len] = 0;
	if (size) *size = len;
	return data;
}

int HKFileExists(const char *path)
{
	std::error_code ec;
	return fs::exists(HostPath(HKResolvePath(path)), ec);
}

int HKMakeDir(const char *path)
{
	std::error_code ec;
	fs::path p = HostPath(HKResolvePath(path));
	fs::create_directory(p, ec);
	return fs::is_directory(p, ec);
}

int HKRenameFile(const char *from, const char *to)
{
	std::error_code ec;
	fs::rename(HostPath(HKResolvePath(from)), HostPath(HKResolvePath(to)), ec);
	return !ec;
}

std::vector<HKDirEntry> HKListDir(const char *dir)
{
	std::vector<HKDirEntry> out;
	std::error_code ec;
	fs::directory_iterator it(HostPath(HKResolvePath(dir)), ec), last;
	for (; !ec && it != last; it.increment(ec))
	{
		HKDirEntry e;
		e.name = it->path().filename().u8string();
		std::error_code direc;
		e.isdir = it->is_directory(direc);
		out.push_back(e);
	}
	return out;
}

int HKHasExt(const char *name, const char *ext)
{
	size_t n = strlen(name), e = strlen(ext);
	if (n < e) return 0;
	for (size_t i = 0; i < e; i++)
		if (tolower((unsigned char)name[n - e + i]) != tolower((unsigned char)ext[i])) return 0;
	return 1;
}
