//The line protocol of edifice-lobby's signaling relay (server/signaling.go, roadmap
//M7d), as GnsLink's signaling client speaks it: the protocol of Valve's
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

//signaling relay address, "host:port" (host name or IPv4; port 1-65535): 1 and its
//parts if valid
int SignalParseAddress(const char *text,char *host,int hostsize,int *port);
//STUN/TURN URLs of a lobby session ("stun:host:port", "turn:host:port?transport=udp")
//as GameNetworkingSockets' native ICE client takes them ("host:port"): 1 if valid
int SignalIceServer(const char *url,const char *scheme,char *out,int size);

#endif
