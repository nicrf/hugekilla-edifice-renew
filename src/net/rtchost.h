//The WebRTC bridge of a native host (roadmap M14 task 3, docs/M7.md), built only
//with EDIFICE_WEBRTC (libdatachannel, beside EDIFICE_GNS). A browser has no UDP
//socket and doesn't speak GameNetworkingSockets: it joins a native host over a
//WebRTC DataChannel instead (src/net/webrtclink.cpp on its side).
//
//While the host listens for P2P connections (the signaling relay hands it the lines
//sent to its identity, gnssignal.cpp), each WebRTC offer from a browser (rtc1:
//lines, signaling.h) makes a libdatachannel PeerConnection with the same STUN/TURN
//servers as GameNetworkingSockets; it answers, trickles its ICE candidates back,
//and takes the browser's DataChannel ("edifice", unordered, no retransmission).
//Its messages are those of GnsLink: [uint8 channel][Sock datagram], one channel for
//what the info port carries and one for the player's own port. Each browser is
//named by a pseudo-address 0.1.x.y:0 (GnsLink's are 0.0.x.y), so multiplay.cpp
//handles it like any other client. DTLS (on ICE) proves who is at the other end:
//no join cookie is needed, as for a GameNetworkingSockets connection.
//
//libdatachannel calls back on its own threads: everything they touch is behind a
//mutex, and the game thread does the rest (new peers, closing) when it reads its
//info link, as with GnsLink's callbacks.

#ifndef RTCHOSTH
#define RTCHOSTH

#include "datagramlink.h"

struct SignalRtc;

//the STUN/TURN servers of the lobby session, as NetP2PStart gives them to
//GameNetworkingSockets: comma-separated "host:port" lists, TURN user names and
//passwords in the same order; relayonly: only TURN relay candidates (p2p=relay)
void RtcHostConfigure(const char *stun,const char *turn,const char *turnusers,const char *turnpasses,int relayonly);
//a WebRTC signal from sender (the relay vouched for it), while hosting: queued for
//the game thread (called on the signaling thread)
void RtcHostSignal(const char *sender,const SignalRtc &signal);
//a host's info link: inner (its GnsLink) plus the browsers' DataChannels. Owns inner.
DatagramLink *RtcHostInfoLink(DatagramLink *inner);
//the data link of the browser named peer (a client's request address), 0 if peer
//is no browser
DatagramLink *RtcHostOpenData(const SOCKADDR_IN *peer);
//1 if peer names a browser whose DataChannel is open (DTLS proved who it is)
int RtcHostIsPeer(const SOCKADDR_IN *peer);
//closes every peer (links must be closed first)
void RtcHostShutdown();

#endif
