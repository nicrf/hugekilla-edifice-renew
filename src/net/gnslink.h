//GnsLink: a DatagramLink over GameNetworkingSockets connections (roadmap M7b, M7d,
//docs/M7.md), built only with EDIFICE_GNS. Connections by IP address (transport=gns)
//or P2P (Internet games of the lobby: ICE through the NATs, a TURN relay as a last
//resort, signaled through the lobby, see gnssignal.h), always encrypted. Sock's
//datagrams travel as unreliable no-Nagle messages: Sock keeps its own grouping,
//reliable channel and acks on top, unchanged.
//
//One connection per client and server. The two kinds of ports of the UDP game become a
//channel byte in front of each message: [uint8 channel][Sock datagram]
//  GNSCHANNEL_INFO  what the info port carries (REQUESTINFO, INFO, REQUESTENTER, ENTER)
//  GNSCHANNEL_DATA  what a client's own port carries (everything once joined)
//Each connection is named by a SOCKADDR_IN, its pseudo-address, so that multiplay.cpp
//keeps using dest, filterip and the server list: on the client the address it was
//opened for (the server's IP and info port; for P2P the host's address and info port
//as the lobby lists it, so the join screen finds the INFO answer where it expects
//it), on the server the client's IPv4 address and port as GameNetworkingSockets sees
//them (a made-up 0.0.x.y one if it has none, and always for P2P connections, which
//are named by an identity).
//
//The info link also carries plain UDP (the LAN search, servers and clients that don't
//use GameNetworkingSockets): what it sends to an address that is no connection goes
//through its UdpLink.

#ifndef GNSLINKH
#define GNSLINKH

#include "datagramlink.h"

#define GNSCHANNEL_INFO 0
#define GNSCHANNEL_DATA 1

class UdpLink;
struct GnsConn;

//what P2P needs, from a lobby session (netlink.cpp makes it)
struct GnsP2PConfig
{
	const char *identity;       //ours, str:edifice-...
	const char *token;          //of its session, for the signaling relay: never logged
	const char *signalhost;     //the signaling relay
	int signalport;
	const char *stun;           //STUN servers, "host:port,host:port", may be ""
	const char *turn;           //TURN servers, same form
	const char *turnusers;      //...their user names and passwords, in the same order
	const char *turnpasses;
	int relayonly;              //only TURN relay candidates offered (tests, strict networks)
};

class GnsLink : public DatagramLink
{
public:
	//info link. ip (transport=gns): a host listens for connections on gnsport (host
	//order), a client connects to gnsport of the servers it asks (Connect). A host
	//also listens for P2P connections once StartP2P was called. Takes udp over (may be
	//0) when it succeeds; returns 0, udp untouched, when GameNetworkingSockets can't
	//start or the port can't be listened on.
	static GnsLink *OpenInfo(UdpLink *udp,int host,int gnsport,int ip=1);
	//data link of the connection named peer (server: a client's request address;
	//client: the server's), 0 when peer is no connection (a plain UDP peer)
	static GnsLink *OpenData(const SOCKADDR_IN *peer);
	//the info link opened last, 0 if none is open
	static GnsLink *CurrentInfo();
	//closes everything and stops GameNetworkingSockets (links must be closed first)
	static void Shutdown();
	//P2P: becomes the session's identity (connections opened before with another one
	//are closed), uses its STUN/TURN servers and signaling relay; host info links
	//listen for P2P connections from now on. 1 if done (and, when a host info link is
	//open, it listens).
	static int StartP2P(const GnsP2PConfig *config);

	//client info link: opens a connection to the server at addr (its IP, gnsport); from
	//now on what is sent to addr goes through it. Does nothing if one is already open,
	//or if the link isn't by IP.
	void Connect(const SOCKADDR_IN *addr);
	//client info link: opens a P2P connection to the host identity (StartP2P first),
	//named addr. 1 if it is opening, or a connection is already open for addr.
	int ConnectP2P(const SOCKADDR_IN *addr,const char *identity);
	//data link: its connection (tests: GetConnectionInfo)
	unsigned int Connection();

	~GnsLink();
	int SendTo(const void *data,int size,const SOCKADDR_IN *to);
	int ReceiveFrom(void *data,int maxsize,SOCKADDR_IN *from);
private:
	GnsLink();
	int channel;
	UdpLink *udp;      //info link: plain UDP beside the connections
	int host;          //info link: listens
	int ip;            //info link: by IP too (transport=gns)
	int gnsport;
	unsigned int listen;
	GnsConn *conn;     //data link: its connection
	int nextconn;      //info link: where the next ReceiveFrom starts looking
};

#endif
