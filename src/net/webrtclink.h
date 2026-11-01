//WebRtcLink: the DatagramLink of the web build (roadmap M14 task 3, docs/M7.md),
//built only with Emscripten. The browser has no UDP: it joins a native host's
//Internet game over a WebRTC DataChannel, which the host's bridge takes
//(src/net/rtchost.h). The browser's objects are in src/platform/web/webrtc.js;
//this side signals (rtc1: lines, signaling.h, over the lobby's WebSocket relay)
//and makes datagrams of what comes and goes, as GnsLink does for its connections:
//  - one DataChannel per host, carrying [uint8 channel][Sock datagram], the info
//    channel (REQUESTINFO, INFO, REQUESTENTER, ENTER) and the player's own one;
//  - named by the address and info port the lobby lists for the host, so that the
//    join screen finds its INFO answer where it expects it (JoinNetFind);
//  - the info link opens it (ConnectP2P), the data link of ENTER takes it over.
//Nothing runs between frames but the browser's own callbacks, which only queue:
//the game polls everything when it reads a link.

#ifndef WEBRTCLINKH
#define WEBRTCLINKH

#include "datagramlink.h"

struct LobbySession;
struct WebConn;

class WebRtcLink : public DatagramLink
{
public:
	//info link: what goes to an address with no connection is dropped (no UDP)
	static WebRtcLink *OpenInfo();
	//data link of the connection named peer (the server), 0 if there is none
	static WebRtcLink *OpenData(const SOCKADDR_IN *peer);
	//the info link opened last, 0 if none is open
	static WebRtcLink *CurrentInfo();
	//P2P with this lobby session: its WebSocket relay (signalurl), identity and
	//token, and STUN/TURN servers; relayonly: TURN relay candidates only. 1 if ready
	static int Start(const LobbySession *session,int relayonly);
	//closes every connection and the relay (links must be closed first)
	static void Shutdown();

	//info link: opens a connection to the host identity (Start first), named addr. 1
	//if it is opening, or one is already open for addr.
	int ConnectP2P(const SOCKADDR_IN *addr,const char *identity);

	~WebRtcLink();
	int SendTo(const void *data,int size,const SOCKADDR_IN *to);
	int ReceiveFrom(void *data,int maxsize,SOCKADDR_IN *from);
private:
	WebRtcLink();
	int channel;
	WebConn *conn;     //data link: its connection
	int nextconn;      //info link: where the next ReceiveFrom starts looking
};

#endif
