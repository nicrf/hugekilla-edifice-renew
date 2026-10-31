// Game and network protocol versions, exchanged when a client looks up or joins
// a server (see docs/ROADMAP.md, section 5).
//
// Bump EDIFICE_PROTOCOL_VERSION in the same commit as any change that breaks
// play between two builds:
//  - format, id or meaning of a network message, or of a ServerVars field;
//  - land/scenery generation or its random numbers (both sides must build the
//    same world from the same LandDefs);
//  - quantization, tick rate, transport.
// Both sides must have the same protocol version; there is no negotiation.
// EDIFICE_VERSION_STRING (the release, from CMakeLists.txt) is informative only.

#ifndef EDIFICE_VERSION_H
#define EDIFICE_VERSION_H

#ifndef EDIFICE_VERSION_STRING
#define EDIFICE_VERSION_STRING "dev"
#endif
#define EDIFICE_PROTOCOL_VERSION 2   // 1 = original game, no version sent
#define EDIFICE_NET_MAGIC 0x31564445u // "EDV1", start of the version trailer

// Version trailer appended after the name of REQUESTENTER and INFO messages:
//   uint32 magic | uint16 protocol | uint32 layout hash | uint8 n | char version[n]
// little-endian. Original builds ignore it (they read the name only).
#define VERSIONTRAILER_MAX 26

struct PeerVersion
{
	int present;          // 0: original build (protocol 1)
	int protocol;
	unsigned int layout;  // hash of the sizes of the structs sent over the network
	char version[16];
};

int WriteVersionTrailer(char *buf);
void ReadVersionTrailer(const char *buf, int len, PeerVersion *out);
// 1 if a peer can play with this build; otherwise 0 and a readable reason.
int VersionCompatible(const PeerVersion *peer, char *reason, int reasonsize);

#endif
