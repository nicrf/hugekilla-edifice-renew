#ifndef SOCKH
#define SOCKH

#define MAXPACKETSIZE 1000
#define BIGBUFSIZE 50000
#define MAXSIMULRELIABLE 30
#define SIMPLE 5


struct Message
{
	char type;
	char buf[BIGBUFSIZE];
};

struct SimpleMessage
{
	char type;
	char type2;
	DWORD msg;
	DWORD msg2;
};


class Sock
{
public:
	DWORD packetsreceived,packetssent;
private:
	struct buf
	{
		char type;
		char data[MAXPACKETSIZE];
	};
	struct rbuf
	{
		buf data;
		int size;
		WORD id,num;
		int timetillresend;
	};
		
	struct reliabledata
	{
		char *data;
		int size;
		reliabledata *next;
		int callback;
	};
	int recvbufpos;
	int recvbufsize;
	int sendbufpos;
	
	WORD reliablebufid;
	WORD reliablebufnum;
	int reliablerecvdatasize;
	WORD lastreliableid;
	int reliableintransmit;
	buf sendbuf;
	buf recvbuf;
	rbuf reliablebuf[MAXSIMULRELIABLE];
	DWORD reliablerecvbufpos;
	DWORD reliablerecvbufsize;
	char reliablerecvdata[BIGBUFSIZE];
	reliabledata *reliablesenddata;
	void ResendReliable(rbuf *curbuf);
	void PackReliable();
	DWORD GetCheckSum(char *buf,int size);
public:
	Sock();
	SOCKET sock;
	SOCKADDR_IN dest;
	void CheckReliableResend();
	int Send(void *data,int size);
	int SendReliable(void *data,int size,int callback);
	int Receive(void *data,SOCKADDR_IN *source,int maxsize=sizeof(Message));
	int fromack;   //last Receive returned a local reliable-ack callback (not peer data)
	void ResetReliable();
	void ResetRecvReliable();
	void Flush();
	
};
#endif