//Which DatagramLink each Sock of multiplay.cpp gets (roadmap M7, docs/M7.md).
//
//By default plain UDP (UdpLink), exactly as before M7. With EDIFICE_GNS and
//transport=gns in net.ini (or HK_TRANSPORT=gns), direct IP games go through
//GameNetworkingSockets (GnsLink, encrypted): the host also listens for connections on
//gnsport (net.ini, default datasendport, a port the UDP game leaves free), and a
//client connects to gnsport of the server it asks by IP. The LAN search, and the
//servers and clients that only speak UDP, stay on UDP.
//
//Internet games (M7d): with EDIFICE_GNS, and unless net.ini says p2p=0 (HK_P2P
//overrides it), a host announced on the lobby also listens for P2P connections
//under its session's identity, and a client joins such a game P2P (no port to open
//at the host's: ICE through the NATs, TURN relay when it must). p2p=relay: only
//TURN relay candidates are offered, the traffic goes through the lobby's relay
//(tests; networks that let nothing else through).

#ifndef NETLINKH
#define NETLINKH

class DatagramLink;
struct LobbySession;

//infosock (info port, broadcast); host: the server's. p2p: GameNetworkingSockets
//for P2P even with transport=udp (an Internet host, a P2P join)
DatagramLink *NetOpenInfoLink(WORD port,int host,int p2p=0);
//client datasock once server sent ENTER: its connection, else UDP on port
DatagramLink *NetOpenDataLink(WORD port,const SOCKADDR_IN *server);
//server: the link of a new client whose request came from source, 0 when it is a UDP
//client (it gets its own UDP port)
DatagramLink *NetOpenPeerLink(const SOCKADDR_IN *source);
//server: 1 if source is a GameNetworkingSockets connection (encrypted, its handshake
//proved the address: no join cookie needed, M11 phase 2), 0 for plain UDP
int NetSourceIsConnection(const SOCKADDR_IN *source);
//client: a server is asked by IP (dest: its address and info port)
void NetQueryHost(const SOCKADDR_IN *dest);
//at exit, once every Sock is closed
void NetLinkShutdown();
//1 if link is a GameNetworkingSockets info link
int NetLinkIsGns(DatagramLink *link);

//P2P: 0 off (or built without GameNetworkingSockets), 1 on, 2 relay only
int NetP2PMode();
//P2P with this lobby session (lobbyurl: the lobby, whose host is the signaling
//relay when the session names none). 1 if P2P is ready (a host: listens)
int NetP2PStart(const LobbySession *session,const char *lobbyurl);
//client: opens a P2P connection to the host identity, named dest (the address and
//info port the lobby lists): what is sent to dest goes through it. 1 if opening.
int NetQueryHostP2P(const SOCKADDR_IN *dest,const char *identity);

#endif
