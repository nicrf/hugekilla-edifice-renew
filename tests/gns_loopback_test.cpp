// GameNetworkingSockets smoke test: a listen socket and a client on 127.0.0.1,
// one reliable message each way, and the connection must be encrypted.
// Built only with -DEDIFICE_GNS=ON (see vcpkg.json and BUILD.md).

#include <stdio.h>
#include <string.h>
#include <chrono>
#include <thread>
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

static HSteamListenSocket listensocket = k_HSteamListenSocket_Invalid;
static HSteamNetConnection serverside = k_HSteamNetConnection_Invalid;
static int clientconnected;

static void OnStatusChanged(SteamNetConnectionStatusChangedCallback_t *info)
{
	ISteamNetworkingSockets *net = SteamNetworkingSockets();
	if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting &&
		info->m_info.m_hListenSocket == listensocket)
	{
		net->AcceptConnection(info->m_hConn);
		serverside = info->m_hConn;
	}
	if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connected &&
		info->m_info.m_hListenSocket == k_HSteamListenSocket_Invalid)
		clientconnected = 1;
	if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally ||
		info->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer)
		printf("connection problem: %s\n", info->m_info.m_szEndDebug);
}

static int Receive(HSteamNetConnection conn, char *out, int size)
{
	ISteamNetworkingMessage *msg = 0;
	if (SteamNetworkingSockets()->ReceiveMessagesOnConnection(conn, &msg, 1) != 1)
		return 0;
	int n = msg->m_cbSize < size - 1 ? msg->m_cbSize : size - 1;
	memcpy(out, msg->m_pData, n);
	out[n] = 0;
	msg->Release();
	return 1;
}

int main()
{
	SteamDatagramErrMsg err;
	if (!GameNetworkingSockets_Init(0, err))
	{
		printf("GameNetworkingSockets_Init failed: %s\n", err);
		return 1;
	}
	SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(OnStatusChanged);
	ISteamNetworkingSockets *net = SteamNetworkingSockets();

	SteamNetworkingIPAddr addr;
	addr.Clear();
	addr.ParseString("127.0.0.1:27150");
	listensocket = net->CreateListenSocketIP(addr, 0, 0);
	HSteamNetConnection client = net->ConnectByIPAddress(addr, 0, 0);
	if (listensocket == k_HSteamListenSocket_Invalid || client == k_HSteamNetConnection_Invalid)
	{
		printf("could not create the sockets\n");
		return 1;
	}

	int failures = 0;
	char got[64] = "";
	int sent = 0, step = 0;
	for (int i = 0; i < 1000 && step < 2; i++)   // up to ~10 s
	{
		net->RunCallbacks();
		if (clientconnected && serverside != k_HSteamNetConnection_Invalid && !sent)
		{
			net->SendMessageToConnection(client, "hello server", 13, k_nSteamNetworkingSend_Reliable, 0);
			sent = 1;
		}
		if (step == 0 && Receive(serverside, got, sizeof(got)))
		{
			printf("server got: %s\n", got);
			if (strcmp(got, "hello server")) failures++;
			net->SendMessageToConnection(serverside, "hello client", 13, k_nSteamNetworkingSend_Reliable, 0);
			step = 1;
		}
		if (step == 1 && Receive(client, got, sizeof(got)))
		{
			printf("client got: %s\n", got);
			if (strcmp(got, "hello client")) failures++;
			step = 2;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	if (step < 2)
	{
		printf("messages did not go through (step %d)\n", step);
		failures++;
	}

	SteamNetConnectionInfo_t info;
	if (net->GetConnectionInfo(client, &info))
	{
		int encrypted = !(info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Unencrypted);
		printf("encrypted: %s\n", encrypted ? "yes" : "NO");
		if (!encrypted) failures++;
	}
	else
		failures++;

	net->CloseConnection(client, 0, 0, false);
	net->CloseListenSocket(listensocket);
	GameNetworkingSockets_Kill();
	printf(failures ? "gns loopback: %d failure(s)\n" : "gns loopback: OK\n", failures);
	return failures ? 1 : 0;
}
