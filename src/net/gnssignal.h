//Signaling client of GnsLink's P2P connections (roadmap M7d, docs/M7.md), built
//only with EDIFICE_GNS. Modeled on Valve's examples/trivial_signaling_client.cpp,
//plus the lobby's authentication (signaling.h has the protocol).
//
//A thread keeps one connection to the signaling relay of edifice-lobby,
//authenticated by the lobby session (its token is only ever sent there, never
//logged): a WebSocket on the lobby's HTTPS API (wss://.../v1/signal, built with
//libcurl), or else plain TCP (the token then travels in clear text). GameNetworkingSockets hands it the signals of a P2P connection (from any
//thread); what comes back is checked before GameNetworkingSockets sees it:
//  - the relay vouches for the sender of each line (it checked its token), and the
//    signal must name that same sender as its from_identity;
//  - a client takes signals only from the hosts it connects to (GnsSignalExpect);
//  - a connection request is only accepted while hosting (GnsSignalListen), and
//    only if it comes from the identity the relay vouched for.
//The thread connects when there is something to send or while hosting, and comes
//back with a growing delay when the relay can't be reached.

#ifndef GNSSIGNALH
#define GNSSIGNALH

#include <string>
#include <steam/steamnetworkingcustomsignaling.h>

//(re)starts the client as identity, with the token of its session, for the relay at
//url (ws:// or wss://, preferred when this build has libcurl; 0 or "" if none), or
//else at host:port over TCP. Nothing changes if all are the same as before.
//HK_SIGNAL_TCP=1 takes the TCP relay even when there is a WebSocket one (tests).
void GnsSignalStart(const char *host,int port,const char *url,const char *identity,const char *token);
//stops the thread (after GameNetworkingSockets_Kill: connections may still send)
void GnsSignalStop();
//a line for the relay ("DESTINATION PAYLOAD\n"), sent once connected; from any
//thread (the WebRTC bridge, rtchost.h)
void GnsSignalSendLine(const std::string &line);
//the signaling of a new connection to peer (taken by ConnectP2PCustomSignaling)
ISteamNetworkingConnectionSignaling *GnsSignalFor(const char *peer);
//client: signals from peer are now expected (the host it connects to)
void GnsSignalExpect(const char *peer);
//host: number of P2P listen sockets open (connection requests accepted if > 0)
void GnsSignalListen(int count);

#endif
