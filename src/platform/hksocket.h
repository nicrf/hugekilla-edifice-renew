//BSD sockets on every platform: Winsock 2 on Windows, whose names (SOCKET,
//SOCKADDR_IN, INVALID_SOCKET, closesocket) are defined on POSIX systems.

#ifndef HKSOCKET_H
#define HKSOCKET_H

#include "hktypes.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN   //no GDI nor multimedia: winsock2.h includes windows.h
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef ULONG in_addr_t;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int SOCKET;
typedef struct sockaddr_in SOCKADDR_IN;
#define INVALID_SOCKET (-1)
static inline int closesocket(SOCKET s) { return close(s); }
#endif

//Winsock needs a WSAStartup before the first socket; nothing to do elsewhere
static inline void HKSocketsInit()
{
#ifdef _WIN32
	WSADATA data;
	WSAStartup(MAKEWORD(2, 2), &data);
#endif
}

static inline void HKSocketsQuit()
{
#ifdef _WIN32
	WSACleanup();
#endif
}

#endif
