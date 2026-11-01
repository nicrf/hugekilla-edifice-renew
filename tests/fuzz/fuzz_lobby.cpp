// Answers of the lobby (src/net/lobby.cpp, roadmap M7c): whatever a hostile or
// broken server sends, the readers stay in bounds, and what they accept is valid:
// printable NUL-terminated texts, IPv4 addresses, numbers in range.
#include "lobby.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_CHECK(c) do { if(!(c)) { fprintf(stderr,"%s:%d: fuzz check failed: %s\n",__FILE__,__LINE__,#c); abort(); } } while(0)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size);

static void Printable(const char *s,size_t room)
{
	size_t n=strnlen(s,room);
	FUZZ_CHECK(n<room);
	for(size_t i=0;i<n;i++) FUZZ_CHECK(s[i]>=32&&s[i]<127);
}

static void CheckGame(const LobbyGame &g)
{
	Printable(g.id,sizeof(g.id));
	Printable(g.name,sizeof(g.name));
	Printable(g.address,sizeof(g.address));
	Printable(g.version,sizeof(g.version));
	Printable(g.visibility,sizeof(g.visibility));
	Printable(g.identity,sizeof(g.identity));
	FUZZ_CHECK(g.id[0]&&g.name[0]&&g.address[0]);
	FUZZ_CHECK(g.port>=1&&g.port<=65535&&g.protocol>=0&&g.protocol<=65535);
	FUZZ_CHECK(g.players>=0&&g.players<=255&&g.maxplayers>=0&&g.maxplayers<=255&&g.mode>=0&&g.mode<=255);
	FUZZ_CHECK(!strchr(g.identity,' '));
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
	if(size>LOBBY_MAXREPLY) return 0;
	// a copy of exactly size bytes: ASan sees any read past the answer
	char *json=(char *)malloc(size?size:1);
	memcpy(json,data,size);
	int len=(int)size;

	static LobbyGame games[LOBBY_MAXGAMES];
	int n=LobbyParseList(json,len,games,LOBBY_MAXGAMES);
	FUZZ_CHECK(n>=-1&&n<=LOBBY_MAXGAMES);
	for(int i=0;i<n;i++) CheckGame(games[i]);

	LobbyGame g;
	if(LobbyParseGame(json,len,&g)) CheckGame(g);

	char id[LOBBY_CODELEN+1],token[65],identity[65],msg[100];
	int refresh;
	if(LobbyParseAnnounced(json,len,id,sizeof(id),token,sizeof(token),&refresh))
	{
		Printable(id,sizeof(id));
		Printable(token,sizeof(token));
		FUZZ_CHECK(id[0]&&token[0]&&refresh>=2&&refresh<=300);
	}
	if(LobbyParseSession(json,len,identity,sizeof(identity),token,sizeof(token)))
	{
		Printable(identity,sizeof(identity));
		Printable(token,sizeof(token));
	}
	LobbyParseError(json,len,msg,sizeof(msg));
	Printable(msg,sizeof(msg));
	free(json);
	return 0;
}
