//Which DatagramLink each Sock of multiplay.cpp gets (roadmap M7, docs/M7.md).
//
//By default plain UDP (UdpLink), exactly as before M7. With EDIFICE_GNS and
//transport=gns in net.ini (or HK_TRANSPORT=gns), direct IP games go through
//GameNetworkingSockets (GnsLink, encrypted): the host also listens for connections on
//gnsport (net.ini, default datasendport, a port the UDP game leaves free), and a
//client connects to gnsport of the server it asks by IP. The LAN search, and the
//servers and clients that only speak UDP, stay on UDP.

#ifndef NETLINKH
#define NETLINKH

class DatagramLink;

//infosock (info port, broadcast); host: the server's
DatagramLink *NetOpenInfoLink(WORD port,int host);
//client datasock once server sent ENTER: its connection, else UDP on port
DatagramLink *NetOpenDataLink(WORD port,const SOCKADDR_IN *server);
//server: the link of a new client whose request came from source, 0 when it is a UDP
//client (it gets its own UDP port)
DatagramLink *NetOpenPeerLink(const SOCKADDR_IN *source);
//client: a server is asked by IP (dest: its address and info port)
void NetQueryHost(const SOCKADDR_IN *dest);
//at exit, once every Sock is closed
void NetLinkShutdown();

#endif
