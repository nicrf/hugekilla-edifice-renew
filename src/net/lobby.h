//client of edifice-lobby (server/, roadmap M7c): the list of Internet games.
//HTTP(S) requests run one at a time on a worker thread (libcurl), so the game loop
//never waits for the network: it sends a request, then polls for its answer.
//The answers are small fixed JSON documents, read here by a bounded parser that
//keeps only validated fields (the lobby is a remote server: never trusted).
//Nothing in here depends on the game, so tests/lobby_test.cpp uses it directly;
//the game side (announce, renewal, join screen list) is lobbygame.cpp.

#ifndef LOBBYH
#define LOBBYH

#include <string>

#define LOBBY_MAXGAMES 128           //games kept from a list (MAXLOCALLIST in multiplay.cpp)
#define LOBBY_MAXREPLY (512*1024)    //longer answers are dropped
#define LOBBY_CODELEN 16             //game codes are 6 characters; up to 16 accepted

//a game as the lobby lists it
struct LobbyGame
{
	char id[LOBBY_CODELEN+1];   //game code, letters and digits
	char name[65];
	char address[16];           //IPv4 seen by the lobby, dotted
	int port;                   //host's info port
	char version[16];
	int protocol;
	int players;
	int maxplayers;
	int mode;                   //sv.gamemode
	char visibility[8];         //public, friends, private
	char identity[65];          //GameNetworkingSockets identity (P2P, M7d), may be empty
};

//what a host announces (POST and PUT /v1/servers)
struct LobbyAnnouncement
{
	const char *name;
	int port;
	const char *version;
	int protocol;
	int players;
	int maxplayers;
	int mode;
	const char *visibility;
	const char *identity;
	const char *session;        //token of identity's session: the lobby lists the
	                            //identity only with it (0 or "": no identity)
};

#define LOBBY_MAXICE 4              //STUN and TURN servers kept from a session

struct LobbyIceServer
{
	char url[128];              //turn:host:port?transport=udp
	char username[128];
	char credential[128];
};

//POST /v1/sessions: who we are for P2P (M7d), and how to get through NATs.
//Every text is printable ASCII without spaces or commas (GameNetworkingSockets
//takes the STUN/TURN servers as comma-separated lists).
struct LobbySession
{
	char identity[65];          //GameNetworkingSockets identity, str:edifice-...
	char token[65];             //proves it to the signaling relay: never logged
	char signaling[128];        //host:port of the signaling relay over TCP, "" if not given
	char signalurl[160];        //the same over WebSocket, wss://host/v1/signal, "" if not given
	int numstun;
	char stun[LOBBY_MAXICE][128];   //stun:host:port
	int numturn;
	LobbyIceServer turn[LOBBY_MAXICE];
};

//---- HTTP (worker thread) ----

int LobbyHttpAvailable();   //0 when built without libcurl
//base URL of the lobby, e.g. https://lobby.example.org (http:// or https://, no
//spaces, at most 200 characters). Returns 0, and disables the lobby, otherwise.
int LobbySetURL(const char *url,const char *useragent);
const char *LobbyGetURL();  //"" when not set
//queues a request (path like "/v1/servers"; body JSON or 0; token for
//"Authorization: Bearer", or 0). ipv4: resolve the lobby to IPv4 only, so that it
//sees the host's IPv4 address (the game's UDP is IPv4). Returns the request
//number, 0 if the lobby isn't usable.
int LobbySend(const char *method,const char *path,const char *body,const char *token,int ipv4=0);
//0 while the request runs; then 1 once (the request is forgotten): status is the
//HTTP status, or 0 if the request failed (reply is then the error text).
int LobbyPoll(int request,int *status,std::string *reply);
void LobbyDrop(int request);   //the answer won't be polled (the request still runs)
//waits up to waitms for the queued requests (the DELETE of a closing host), then
//stops the worker, aborting what still runs
void LobbyShutdown(int waitms);

//---- answers (also used by the tests) ----

//{"servers":[...]}: number of valid games stored (invalid entries are skipped),
//-1 if the document is malformed
int LobbyParseList(const char *json,int len,LobbyGame *games,int max);
//one game (GET /v1/servers/{id}): 1 if valid
int LobbyParseGame(const char *json,int len,LobbyGame *game);
//POST /v1/servers: id, token, refresh_seconds (clamped to 2..300). 1 if valid
int LobbyParseAnnounced(const char *json,int len,char *id,int idsize,char *token,int tokensize,int *refresh);
//POST /v1/sessions: identity, token, signaling relays, STUN/TURN servers (invalid
//servers are skipped). 1 if valid
int LobbyParseSession(const char *json,int len,LobbySession *session);
//{"error":"..."}: 1 and the printable text if present
int LobbyParseError(const char *json,int len,char *msg,int size);

//game code typed by a player ("abc-123 "): letters and digits only, upper case,
//1 if it looks like one
int LobbyNormalizeCode(const char *in,char *out,int size);
//JSON body of an announcement; 0 if it doesn't fit
int LobbyAnnouncementJSON(const LobbyAnnouncement *a,char *out,int size);

#endif
