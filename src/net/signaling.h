//The line protocol of edifice-lobby's signaling relay (server/signaling.go, roadmap
//M7d; over TCP or, below, WebSocket), as GnsLink's signaling client speaks it: the protocol of Valve's
//trivial_signaling example (GameNetworkingSockets examples/), plus authentication.
//  first line, to the relay:   IDENTITY TOKEN        (a lobby session)
//  then, to the relay:         DESTINATION HEXPAYLOAD
//  from the relay:             SENDER HEXPAYLOAD     (SENDER checked by the relay)
//                              ERROR text            (bad token: then disconnected)
//The payload is a GameNetworkingSockets signal (a CMsgSteamNetworkingP2PRendezvous
//protobuf) in hexadecimal, as in Valve's example. Lines are at most 4 KB (the relay
//drops the connection otherwise).
//Everything that comes from the relay goes through the readers here, bounded and
//strict (tests/signaling_test.cpp, tests/fuzz/fuzz_signaling.cpp). Nothing in here
//depends on GameNetworkingSockets.

#ifndef SIGNALINGH
#define SIGNALINGH

#include <string>

#define SIGNAL_MAXLINE 4000          //characters, '\n' included (the relay takes 4096)
#define SIGNAL_MAXIDENTITY 64
#define SIGNAL_MAXPAYLOAD ((SIGNAL_MAXLINE-SIGNAL_MAXIDENTITY-2)/2)
#define SIGNAL_MAXTOKEN 64

//a session identity as the lobby gives them, "str:" then letters, digits, '.', '_',
//'-' (at most SIGNAL_MAXIDENTITY characters in all)
int SignalValidIdentity(const char *identity);

//the first line, "IDENTITY TOKEN\n"; 0 if either isn't valid (token: letters and
//digits, at most SIGNAL_MAXTOKEN)
int SignalHelloLine(const char *identity,const char *token,std::string *line);
//"DESTINATION HEXPAYLOAD\n"; 0 if the destination isn't valid or the payload too long
int SignalFormatLine(const char *destination,const void *payload,int size,std::string *line);

//a line from the relay, without its '\n' (a '\r' before it is allowed):
//  1  a signal: sender (an identity) and payload (size in *payloadlen)
// -1  "ERROR ...": the relay refused us
//  0  anything else (ignored)
int SignalParseLine(const char *line,int len,char *sender,int sendersize,
	unsigned char *payload,int maxpayload,int *payloadlen);

//from_identity of a GameNetworkingSockets signal (field 8 of a
//CMsgSteamNetworkingP2PRendezvous, the last one if repeated, like protobuf): 1 and
//the identity if the message is well formed protobuf and has a valid one. The relay
//vouches for the sender of a line; this lets the client check the signal claims
//no one else.
int SignalFromIdentity(const unsigned char *msg,int len,char *identity,int size);

//cuts what the relay sends into lines; a line longer than SIGNAL_MAXLINE is
//skipped whole
class SignalReader
{
public:
	SignalReader() : linelen(0), skipping(0) {}
	void Add(const char *data,int size);
	//the next complete line (without '\n'): 1, or 0 if none is complete yet
	int Next(std::string *line);
	void Clear() { buf.clear(); linelen=skipping=0; }
private:
	std::string buf;
	int linelen;    //characters of the line not complete yet
	int skipping;   //inside an overlong line: dropping up to its '\n'
};

//---- the same relay over WebSocket (RFC 6455) ----
//GET /v1/signal on the lobby's HTTP API (server/websocket.go), so that it goes
//through the lobby's HTTPS (wss://, the token never in clear text) without another
//port. The lines above, one per text frame, without their '\n'.
#define SIGNAL_WS_TEXT 1
#define SIGNAL_WS_BINARY 2
#define SIGNAL_WS_CLOSE 8
#define SIGNAL_WS_PING 9
#define SIGNAL_WS_PONG 10
#define SIGNAL_WS_MAXRESPONSE 4096   //the server's answer to the upgrade, headers included

//"wss://host[:port][/path]" (or ws://, plain text; host a name or IPv4, as in
//SignalParseAddress): 1 and its parts if valid (port 443 or 80 by default, path
//"/" by default, printable without spaces)
int SignalParseURL(const char *url,char *host,int hostsize,int *port,char *path,int pathsize,int *tls);
//the upgrade request for host:port/path; key: 16 random bytes
std::string SignalWsRequest(const char *host,int port,int tls,const char *path,const unsigned char key[16]);
//the server's answer to it in data (len bytes received so far): its length when
//complete and "101 Switching Protocols" to a WebSocket, 0 while incomplete, -1 if
//refused or malformed
int SignalWsResponse(const char *data,int len);
//a client frame (masked with mask, as RFC 6455 wants of clients) appended to out
void SignalWsFrame(int opcode,const char *payload,int size,const unsigned char mask[4],std::string *out);

//cuts what the server sends into frames (unmasked, unfragmented: a server frame
//that isn't is a protocol error); a text or binary frame longer than
//SIGNAL_MAXLINE is skipped whole, like an overlong line
class SignalWsReader
{
public:
	SignalWsReader() : skipping(0) {}
	void Add(const char *data,int size);
	//1 and the next frame (text, binary, ping, pong or close: opcode and payload),
	//0 if none is complete yet, -1 on a protocol error (then close)
	int Next(int *opcode,std::string *payload);
	void Clear() { buf.clear(); skipping=0; }
private:
	std::string buf;
	unsigned long long skipping;   //bytes of an overlong frame still to drop
};

//---- WebRTC signals (roadmap M14 task 3) ----
//A browser joins a native host over a WebRTC DataChannel (rtchost.h, webrtclink.h).
//Its offer, the host's answer and the ICE candidates of both travel over the same
//relay, as lines whose payload is
//  rtc1:K:CID:A[:B]
//K the kind (o offer, a answer, c candidate), CID the browser's connection id (1 to
//SIGNAL_RTC_MAXCID letters and digits, a new one each time it tries), A and B in
//base64url without padding: the SDP (o, a), or the candidate line and its mid (c).
//The relay wants printable payloads without spaces, which these are; the hex
//payloads of GameNetworkingSockets never start with "rtc1:". No JSON on purpose:
//nothing to parse but these fields, on both sides.
#define SIGNAL_RTC_PREFIX "rtc1:"
#define SIGNAL_RTC_MAXCID 16
#define SIGNAL_RTC_MAXTEXT 2700      //an SDP or a candidate, decoded (DataChannel SDPs: ~600)

struct SignalRtc
{
	char kind;                       //'o', 'a' or 'c'
	char cid[SIGNAL_RTC_MAXCID+1];
	std::string a,b;                 //SDP; or candidate and mid (b may be "")
};

//"DESTINATION rtc1:...\n"; 0 if anything isn't valid or the line is too long
int SignalRtcFormat(const char *destination,const SignalRtc *s,std::string *line);
//the payload of a relay line ("rtc1:..."): 1 and the signal if valid (texts
//printable ASCII, SDP line ends and tabs allowed), else 0
int SignalRtcParse(const char *payload,int len,SignalRtc *s);
//a line from the relay, without its '\n': 1 and the sender and signal if it is a
//valid WebRTC signal, else 0 (another kind of line, or malformed)
int SignalRtcParseLine(const char *line,int len,char *sender,int sendersize,SignalRtc *s);

//signaling relay address, "host:port" (host name or IPv4; port 1-65535): 1 and its
//parts if valid
int SignalParseAddress(const char *text,char *host,int hostsize,int *port);
//STUN/TURN URLs of a lobby session ("stun:host:port", "turn:host:port?transport=udp")
//as GameNetworkingSockets' native ICE client takes them ("host:port"): 1 if valid
int SignalIceServer(const char *url,const char *scheme,char *out,int size);

#endif
