// Game and network protocol versions, exchanged when a client looks up or joins
// a server (see docs/ROADMAP.md, section 5).
//
// Bump EDIFICE_PROTOCOL_VERSION in the same commit as any change that breaks
// play between two builds:
//  - format, id or meaning of a network message (its Sync*/Write*/Read* function
//    in multiplay.cpp or sock.cpp), or of a ServerVars field;
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
#define EDIFICE_PROTOCOL_VERSION 4   // 1 = original game, no version sent
                                     // 2 = version handshake; 3 = PCG32 world (worldrandom.h)
                                     // 4 = explicit serialization (wire.h), no raw structs
#define EDIFICE_NET_MAGIC 0x31564445u // "EDV1", start of the version trailer
// Layout field of the trailer. Up to protocol 3 it was a hash of the sizes of the
// structs sent raw; since 4 nothing is sent raw, so it is this constant and is
// not checked (the protocol alone decides). Kept so the trailer layout doesn't
// change and protocol 2/3 builds still read it and refuse cleanly.
#define EDIFICE_NET_LAYOUT 0x4C505845u // "EXPL"

// Version trailer appended after the name of REQUESTENTER and INFO messages:
//   uint32 magic | uint16 protocol | uint32 layout | uint8 n | char version[n]
// little-endian. Original builds ignore it (they read the name only).
#define VERSIONTRAILER_MAX 26

struct PeerVersion
{
	int present;          // 0: original build (protocol 1)
	int protocol;
	unsigned int layout;  // protocol <=3: struct layout hash; 4+: EDIFICE_NET_LAYOUT (informative)
	char version[16];
};

int WriteVersionTrailer(char *buf);
void ReadVersionTrailer(const char *buf, int len, PeerVersion *out);
// 1 if a peer can play with this build; otherwise 0 and a readable reason.
int VersionCompatible(const PeerVersion *peer, char *reason, int reasonsize);

#endif
