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

//decoded SIMPLE message; on the wire (WriteSimple/ReadSimple, sock.cpp) it is
//SIMPLESIZE bytes: type, type2, 2 zero bytes, uint32 msg, uint32 msg2, little-endian
struct SimpleMessage
{
	char type;
	char type2;
	DWORD msg;
	DWORD msg2;
};
#define SIMPLESIZE 12

//a SIMPLE message serialized, ready to send: b, sizeof(b)
struct SimpleBuf
{
	char b[SIMPLESIZE];
};

int WriteSimple(void *out,char type2,DWORD msg,DWORD msg2);   //returns SIMPLESIZE
int ReadSimple(const void *in,int len,SimpleMessage *out);    //0 if too short


#define MAXRELIABLEQUEUE 256            //messages waiting to be sent reliably
#define MAXRELIABLEQUEUEBYTES (1<<20)
#define RELIABLEFIRSTRESEND 150         //ms, doubled at each resend up to RELIABLEMAXRESEND
#define RELIABLEMAXRESEND 2000
#define RELIABLEGIVEUP 30               //resends of one packet before the link is declared dead

class DatagramLink;

class Sock
{
public:
	DWORD packetsreceived,packetssent;
	int filterip;           //drop datagrams that don't come from dest's IP (set once connected)
	int reliablefailed;     //a reliable packet was never acknowledged: the link is dead
	WORD localport;         //server side: port this socket listens on (host order)
	SOCKADDR_IN requestaddr;//server side: address the join request came from
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
		DWORD resendat;   //GetTickCount() time of the next resend
		int resends;
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
	//what the datagrams go through (datagramlink.h), owned by the Sock; 0 when closed.
	//A Sock is copied and zeroed with CopyMemory/ZeroMemory (multiplay.cpp), so it only
	//holds a pointer: the copy takes the link over.
	DatagramLink *link;
	void Open(DatagramLink *l);   //closes the current link first
	void Close();
	SOCKADDR_IN dest;
	void CheckReliableResend();
	int Send(const void *data,int size);
	int SendReliable(const void *data,int size,int callback);
	int Receive(void *data,SOCKADDR_IN *source,int maxsize=sizeof(Message));
	int fromack;   //last Receive returned a local reliable-ack callback (not peer data)
	//tests and fuzzing (tests/fuzz/fuzz_sock.cpp): the next datagram Receive reads is
	//this one (type byte included) instead of one from the socket; data must stay valid
	//until Receive has used it. Works without a link (none open: nothing is sent).
	void Feed(const void *datagram,int size) { feeddata=(const char *)datagram; feedsize=size; }
	const char *feeddata;
	int feedsize;
	void ResetReliable();
	void ResetRecvReliable();
	void Flush();
	
};
#endif