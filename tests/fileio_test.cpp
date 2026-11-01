// Portable file access (src/platform/fileio.cpp): the case-insensitive lookup the
// Windows-made data needs on Linux ("Structures" on disk, "structures" in the code),
// folder listings, UTF-8 names. On a case-insensitive file system (macOS, Windows)
// the lookup has nothing to fix; CI runs this on Linux too.

#include <stdio.h>
#include <string.h>
#include <filesystem>
#include <string>
#include "fileio.h"

namespace fs = std::filesystem;
static int failures;

static void Check(int ok, const char *what)
{
	if (!ok)
	{
		printf("FAILED: %s\n", what);
		failures++;
	}
}

static void WriteText(const std::string &path, const char *text)
{
	FILE *f = HKFileOpen(path.c_str(), "wb");
	if (f)
	{
		fputs(text, f);
		fclose(f);
	}
}

int main()
{
	std::error_code ec;
	fs::path base = fs::temp_directory_path(ec) / "hk_fileio_test";
	fs::remove_all(base, ec);
	std::string root = base.u8string();
	Check(HKMakeDir(root.c_str()), "make the test folder");
	Check(HKMakeDir((root + "/Structures").c_str()), "make Structures");
	Check(HKMakeDir((root + "/Structures/Basic").c_str()), "make Structures/Basic");
	WriteText(root + "/Structures/Basic/Door.TXT", "door");
	int casesensitive = !fs::exists(base / "STRUCTURES", ec);

	//the code's lower-case path finds the file, whatever the case on disk
	std::string lower = root + "/structures/basic/door.txt";
	long size = 0;
	char *data = HKReadFile(lower.c_str(), &size);
	Check(data && size == 4 && !strcmp(data, "door"), "read through a lower-case path");
	delete[] data;
	if (casesensitive)
		Check(HKResolvePath(lower.c_str()) == root + "/Structures/Basic/Door.TXT", "resolved case");
	Check(HKFileExists(lower.c_str()), "exists through a lower-case path");

	//a new file under a folder named in the wrong case lands in the existing folder
	WriteText(root + "/structures/basic/new.txt", "new");
	Check(fs::exists(base / "Structures" / "Basic" / "new.txt", ec), "new file in the existing folder");

	//listing: names as on disk, folders flagged, "." and ".." left out
	std::vector<HKDirEntry> top = HKListDir((root + "/structures").c_str());
	Check(top.size() == 1 && top[0].name == "Basic" && top[0].isdir, "list folders");
	std::vector<HKDirEntry> files = HKListDir((root + "/Structures/Basic").c_str());
	int doors = 0;
	for (size_t i = 0; i < files.size(); i++)
		if (files[i].name == "Door.TXT" && !files[i].isdir) doors++;
	Check(files.size() == 2 && doors == 1, "list files");
	Check(HKListDir((root + "/missing").c_str()).empty(), "list a missing folder");

	Check(HKHasExt("wall3.BMP", ".bmp") && !HKHasExt("bmp", ".bmp") && !HKHasExt("a.pxr", ".bmp"), "extensions");

	//missing files, moves (replacing the destination), UTF-8 names
	Check(!HKReadFile((root + "/nothing.txt").c_str(), &size) && size == 0, "read a missing file");
	Check(!HKFileExists((root + "/nothing.txt").c_str()), "missing file");
	Check(HKMakeDir((root + "/structures/unused").c_str()), "make a folder in the wrong-case parent");
	WriteText(root + "/Structures/unused/new.txt", "old");
	Check(HKRenameFile((root + "/structures/basic/new.txt").c_str(), (root + "/structures/unused/new.txt").c_str()), "move");
	data = HKReadFile((root + "/Structures/unused/new.txt").c_str(), &size);
	Check(data && !strcmp(data, "new"), "move replaces the destination");
	delete[] data;
	Check(!HKRenameFile((root + "/nothing.txt").c_str(), (root + "/b.txt").c_str()), "move a missing file");
	std::string utf8 = root + "/carte \xc3\xa9t\xc3\xa9.map";
	WriteText(utf8, "map");
	Check(HKFileExists(utf8.c_str()), "UTF-8 name");
	int found = 0;
	std::vector<HKDirEntry> all = HKListDir(root.c_str());
	for (size_t i = 0; i < all.size(); i++)
		if (all[i].name == "carte \xc3\xa9t\xc3\xa9.map") found++;
	Check(found == 1, "UTF-8 name listed");

	fs::remove_all(base, ec);
	printf("%s (%s file system)\n", failures ? "fileio_test failed" : "fileio_test passed",
		casesensitive ? "case-sensitive" : "case-insensitive");
	return failures != 0;
}
