// Game and network protocol versions, exchanged when a client looks up or joins
// a server (see docs/ROADMAP.md, section 5).
//
// Bump EDIFICE_PROTOCOL_VERSION in the same commit as any change that breaks
// play between two builds:
//  - format, id or meaning of a network message (its Sync*/Write*/Read* function
//    in netmsg.h, landwire.h or sock.cpp), or of a ServerVars field; the map files
//    (src/game/mapfile.h) have their own format version;
//  - land/scenery generation or its random numbers (both sides must build the
//    same world from the same LandDefs);
//  - quantization, tick rate, transport;
//  - a new message type (an older build would ignore it, or log "Unknown
//    message received" for every packet), e.g. VOICE in protocol 5.
// Both sides must have the same protocol version. The one negotiation within a
// protocol is the capability byte of REQUESTENTER and INFO below (HELLOCAP_*):
// bytes appended where older builds of the same protocol stop reading, and new
// messages (ENTERCOOKIE; HITMODE, SERVERHIT) or meanings (DELETEPLAYER that keeps
// the slots) only sent to a peer that announced it understands them.
// EDIFICE_VERSION_STRING (the release, from CMakeLists.txt) is informative only.

#ifndef EDIFICE_VERSION_H
#define EDIFICE_VERSION_H

#ifndef EDIFICE_VERSION_STRING
#define EDIFICE_VERSION_STRING "dev"
#endif
#define EDIFICE_PROTOCOL_VERSION 5   // 1 = original game, no version sent
                                     // 2 = version handshake; 3 = PCG32 world (worldrandom.h)
                                     // 4 = explicit serialization (wire.h), no raw structs
                                     // 5 = VOICE message (proximity voice chat, voicemsg.h)
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

// Capabilities (M11 phases 2, 3a and 3b, docs/M11.md), still protocol 5. After the version
// trailer, where builds before them stop reading:
//   REQUESTENTER: ... trailer | uint8 caps | cookie[HELLOCOOKIE_SIZE] if caps & HELLOCAP_HASCOOKIE
//   INFO:         ... trailer | 4 bytes of details | uint8 caps
#define HELLOCAP_COOKIE 1        // REQUESTENTER: the client answers ENTERCOOKIE; INFO: the server sends it
#define HELLOCAP_HASCOOKIE 2     // REQUESTENTER: the cookie follows
#define HELLOCAP_REQUIRECOOKIE 4 // INFO: clients without HELLOCAP_COOKIE are refused (ac_requirecookie)
#define HELLOCAP_SERVERHITS 8    // REQUESTENTER: the client obeys HITMODE/SERVERHIT; INFO: the server
                                 // decides hits (ac_serverhits, M11 phase 3a)
#define HELLOCAP_STABLESLOTS 16  // REQUESTENTER: the client understands PLAYERLEFT_KEEPSLOTS (DELETEPLAYER
                                 // without renumbering, M11 phase 3b); INFO: the server keeps slots
#define HELLOCAP_POSSEQ 32       // REQUESTENTER: its PLAYERPOSITIONs end with a uint16 sequence number
                                 // (POSITIONSEQSIZE, netmsg.h); the server drops those without one
#define HELLOCOOKIE_SIZE 8
#define HELLOCAPS_MAX (1+HELLOCOOKIE_SIZE)

struct HelloCaps
{
	int caps;            // 0: a build without capabilities
	unsigned char cookie[HELLOCOOKIE_SIZE];   // valid if caps & HELLOCAP_HASCOOKIE
};

int WriteVersionTrailer(char *buf);
void ReadVersionTrailer(const char *buf, int len, PeerVersion *out);
// caps byte of a REQUESTENTER, and the cookie if not 0; returns its size
int WriteHelloCaps(char *buf, int caps, const unsigned char *cookie);
// buf, len: from the start of the version trailer of a REQUESTENTER. caps 0 if
// there is no trailer or nothing after it; HELLOCAP_HASCOOKIE dropped if the
// cookie is cut short.
void ReadHelloCaps(const char *buf, int len, HelloCaps *out);
// 1 if a peer can play with this build; otherwise 0 and a readable reason.
int VersionCompatible(const PeerVersion *peer, char *reason, int reasonsize);

#endif
