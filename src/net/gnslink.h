//GnsLink: a DatagramLink over GameNetworkingSockets connections (roadmap M7b,
//docs/M7.md), built only with EDIFICE_GNS. Connections by IP address for now (no P2P),
//always encrypted. Sock's datagrams travel as unreliable no-Nagle messages: Sock keeps
//its own grouping, reliable channel and acks on top, unchanged.
//
//One connection per client and server. The two kinds of ports of the UDP game become a
//channel byte in front of each message: [uint8 channel][Sock datagram]
//  GNSCHANNEL_INFO  what the info port carries (REQUESTINFO, INFO, REQUESTENTER, ENTER)
//  GNSCHANNEL_DATA  what a client's own port carries (everything once joined)
//Each connection is named by a SOCKADDR_IN, its pseudo-address, so that multiplay.cpp
//keeps using dest, filterip and the server list: on the client the address it was
//opened for (the server's IP and info port), on the server the client's IPv4 address
//and port as GameNetworkingSockets sees them (a made-up 0.0.x.y one if it has none).
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

class GnsLink : public DatagramLink
{
public:
	//info link. host: listens for connections on gnsport (host order); a client
	//connects to gnsport of the servers it asks (Connect). Takes udp over (may be 0)
	//when it succeeds; returns 0, udp untouched, when GameNetworkingSockets can't start
	//or the port can't be listened on.
	static GnsLink *OpenInfo(UdpLink *udp,int host,int gnsport);
	//data link of the connection named peer (server: a client's request address;
	//client: the server's), 0 when peer is no connection (a plain UDP peer)
	static GnsLink *OpenData(const SOCKADDR_IN *peer);
	//the info link opened last, 0 if none is open
	static GnsLink *CurrentInfo();
	//closes everything and stops GameNetworkingSockets (links must be closed first)
	static void Shutdown();

	//client info link: opens a connection to the server at addr (its IP, gnsport); from
	//now on what is sent to addr goes through it. Does nothing if one is already open.
	void Connect(const SOCKADDR_IN *addr);
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
	int gnsport;
	unsigned int listen;
	GnsConn *conn;     //data link: its connection
	int nextconn;      //info link: where the next ReceiveFrom starts looking
};

#endif
