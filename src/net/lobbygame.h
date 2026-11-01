//game side of the edifice-lobby client (roadmap M7c): a hosted game announced and
//kept listed, the Internet games and game codes of the join screen. lobby.h does
//the HTTP and the JSON; nothing here waits for the network.
//net.ini: lobby=<https://...> (empty: no Internet games), servervisibility=
//lan (not announced), public (listed), friends or private (joined by game code);
//p2p= (netlink.h) whether Internet games are joined P2P.

#ifndef LOBBYGAMEH
#define LOBBYGAMEH

#include "lobby.h"

//1 if the lobby can be used; otherwise why not, in why (may be 0)
int LobbyReady(char *why=0,int whysize=0);

//a lobby session (M7d): our identity for P2P, host and client alike. Start: 1 if
//one is ready, 0 if it is being asked for, -1 if the lobby can't be used. Result: 0
//while waiting, 1 ready, -1 failed (why in err). Get: the session, 0 if none.
int LobbySessionStart();
int LobbySessionResult(char *err,int errsize);
const LobbySession *LobbySessionGet();

//host (multiplay.cpp): 1 if the server about to start will be announced (net.ini
//servervisibility, a usable lobby), so that it listens for P2P connections
int LobbyHostInternet();
//called when the server starts, every frame, and when it stops
void LobbyHostStart();
void LobbyHostLoop(int players);
void LobbyHostStop();
const char *LobbyHostCode();    //the game code once listed, else ""
const char *LobbyHostVisibility();
void LobbyQuit();               //at exit: lets the DELETE of the game go out (2 s at most)

//join screen: the public games, and one game by its code. Each returns 0 if the
//request can't be sent (see LobbyReady). The *Result functions return 0 while
//waiting, then once 1 (games or game filled) or -1 (error, in err).
int LobbyListRequest();
int LobbyListResult(LobbyGame *games,int max,int *count,char *err,int errsize);
int LobbyLookupRequest(const char *code);
int LobbyLookupResult(LobbyGame *game,char *err,int errsize);

#endif
