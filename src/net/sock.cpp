//Sock is a class that sends datagrams through a DatagramLink (UDP: UdpLink).  It can be used to send 
//anything needed, and can also send data reliably in case of packetloss.  The reliable
//transfer can be used to send large amounts of data also.  If many small packets are being
//sent, it can group them together to save bandwidth.



#include "multiplay.h"
#include "sock.h"
#include "redrawing.h"
#include "wire.h"
#include "datagramlink.h"

#define RELIABLE -1
#define LASTRELIABLE -2
#define ACK -3

#define RELIABLEPACKETSIZE (int)450

int datasendthissec;
int datarecvthissec;
int datasendthisframe;
int datarecvthisframe;

int WriteSimple(void *out,char type2,DWORD msg,DWORD msg2)
{
	ByteWriter w(out,SIMPLESIZE);
	w.U8(SIMPLE);
	w.U8((unsigned char)type2);
	w.U16(0);
	w.U32(msg);
	w.U32(msg2);
	return SIMPLESIZE;
}

int ReadSimple(const void *in,int len,SimpleMessage *out)
{
	ByteReader r(in,len);
	out->type=(char)r.U8();
	out->type2=(char)r.U8();
	r.Skip(2);
	out->msg=r.U32();
	out->msg2=r.U32();
	return r.Ok();
}


Sock::Sock()
{
	ZeroMemory(this,sizeof(*this));
	dest.sin_family=2;
	reliablesenddata=0;
}

void Sock::Open(DatagramLink *l)
{
	Close();
	link=l;
}

void Sock::Close()
{
	delete link;
	link=0;
}

//sends all data waiting to be sent

void Sock::Flush()
{
	sendbuf.type=0;
	if(!sendbufpos) return;
	if(link) link->SendTo(&sendbuf,sendbufpos+1,&dest);
	packetssent++;
	datasendthissec+=sendbufpos+1+16;
	datasendthisframe+=sendbufpos+1+16;
	sendbufpos=0;
}

int Sock::Send(const void *data,int size)
{
	if(size+sendbufpos>MAXPACKETSIZE) Flush();
	WirePutU16(&sendbuf.data[sendbufpos],size);
	sendbufpos+=2;
	CopyMemory(&sendbuf.data[sendbufpos],data,size);
	sendbufpos+=size;
	return 1;
}

//checks for data received by the socket, decides weather they are reliable, or are a groups of packets.
//returns the size of the message copied into data (0 when there is nothing more to read for now).
//every size read from the network is checked before use (roadmap M2a): a malformed packet is
//dropped, and data never receives more than maxsize bytes. fromack is set when the message is a
//local "reliable data acknowledged" callback rather than something sent by the peer.

int Sock::Receive(void *data,SOCKADDR_IN *source,int maxsize)
{
	fromack=0;
	if(!link&&!feeddata) return 0;
	for(;;)
	{
		//messages left from the last complete reliable transfer
		if(reliablerecvbufpos<reliablerecvbufsize)
		{
			DWORD left=reliablerecvbufsize-reliablerecvbufpos;
			DWORD dwsize=0;
			if(left>=4) dwsize=WireGetU32(&reliablerecvdata[reliablerecvbufpos]);
			if(left>=4&&dwsize<=left-4&&dwsize<=(DWORD)maxsize)
			{
				reliablerecvbufpos+=4;
				CopyMemory(data,&reliablerecvdata[reliablerecvbufpos],dwsize);
				reliablerecvbufpos+=dwsize;
				return dwsize;
			}
			reliablerecvbufpos=reliablerecvbufsize;   //malformed: drop the rest
		}
		//messages left in the last datagram
		if(recvbufpos<recvbufsize)
		{
			WORD wsize=0;
			int ok=recvbufpos+2<=recvbufsize;
			if(ok) wsize=WireGetU16(&recvbuf.data[recvbufpos]);
			if(ok&&recvbufpos+2+wsize<=recvbufsize&&wsize<=maxsize)
			{
				recvbufpos+=2;
				CopyMemory(data,&recvbuf.data[recvbufpos],wsize);
				recvbufpos+=wsize;
				return wsize;
			}
			recvbufpos=recvbufsize;   //malformed: drop the rest
		}

		//next datagram
		int received;
		if(feeddata)
		{
			//as recvfrom: a datagram larger than the buffer is cut; source is left as is
			received=feedsize<(int)sizeof(recvbuf)?feedsize:(int)sizeof(recvbuf);
			if(received>0) CopyMemory(&recvbuf,feeddata,received);
			feeddata=0;
		}
		else
		{
			if(!link) return 0;
			received=link->ReceiveFrom(&recvbuf,sizeof(recvbuf),source);
			if(received==0) return 0;   //nothing waiting
		}
		packetsreceived++;
		if(received<=0) return 0;
		//once connected, only the peer's IP is listened to (any port: NATs may change it)
		if(filterip&&dest.sin_addr.s_addr&&source->sin_addr.s_addr!=dest.sin_addr.s_addr)
			continue;
		recvbufsize=received-1;   //bytes after the type
		recvbufpos=recvbufsize;   //consumed unless it turns out to hold grouped messages
		datarecvthissec+=recvbufsize+16;
		datarecvthisframe+=recvbufsize+16;
		if(recvbuf.type==ACK)
		{
			if(recvbufsize<4) continue;
			for(int i=0;i<MAXSIMULRELIABLE;i++)
			{
				if(reliablebuf[i].size&&reliablebuf[i].id==WireGetU16(&recvbuf.data[0])&&reliablebuf[i].num==WireGetU16(&recvbuf.data[2])&&reliablesenddata)
				{
					reliablebuf[i].size=0;
					reliableintransmit--;
					char callback=0;
					fromack=1;
					if(reliablesenddata->size)
					{
						if(reliablesenddata->size<=RELIABLEPACKETSIZE) //will be last one
						{
							if(reliableintransmit==0)   //only send last one if no more are in transmit
							{
								reliablebufnum++;
								PackReliable();
							}
						}
						else
						{
							reliablebufnum++;
							PackReliable();
						}
						callback=0;
					}
					else
					{
						callback=reliablesenddata->callback;
						reliabledata *next=reliablesenddata->next;
						delete []reliablesenddata->data;
						delete reliablesenddata;
						reliablesenddata=next;
						if(reliablesenddata)
						{
							reliablebufid++;
							reliablebufnum=0;
							PackReliable();
						}
					}
					if(maxsize<SIMPLESIZE) return 0;
					return WriteSimple(data,callback,0,0);
				}
			}
			continue;
		}
		if(recvbuf.type==RELIABLE||recvbuf.type==LASTRELIABLE)
		{
			if(recvbufsize<8) continue;   //checksum, id, part number
			if(GetCheckSum(&recvbuf.data[4],recvbufsize-4)!=WireGetU32(recvbuf.data))
			{
				*(char *)data=0;
				return 1;
			}
			//answer where the data came from (lets clients behind a NAT through),
			//only once the packet passed the checksum, and never to another IP
			if(!dest.sin_addr.s_addr||dest.sin_addr.s_addr==source->sin_addr.s_addr)
				CopyMemory(&dest,source,sizeof(dest));
			buf ack;
			ack.type=ACK;
			memcpy(&ack.data[0],&recvbuf.data[4],4);   //id, part number
			if(link) link->SendTo(&ack,1+4,&dest);
			packetssent++;
			WORD id=WireGetU16(&recvbuf.data[4]);
			if(id!=(WORD)(lastreliableid+1))   //WORD: keeps working after 65535 transfers
			{
				*(char *)data=0;
				return 1;
			}
			if(recvbuf.type==LASTRELIABLE)
				lastreliableid=id;
			int rbufpos=WireGetU16(&recvbuf.data[6])*RELIABLEPACKETSIZE;
			int chunk=recvbufsize-8;
			if(rbufpos+chunk>BIGBUFSIZE)
			{
				*(char *)data=0;
				return 1;
			}
			CopyMemory(&reliablerecvdata[rbufpos],&recvbuf.data[8],chunk);
			if(recvbuf.type==LASTRELIABLE)
			{
				reliablerecvbufpos=0;
				reliablerecvbufsize=rbufpos+chunk;
				continue;   //returned from the reliable buffer at the top of the loop
			}
			*(char *)data=0;
			return 1;
		}
		recvbufpos=0;   //grouped messages: read at the top of the loop
	}
}

//used internally

void Sock::ResendReliable(rbuf *curbuf)
{
	if(link) link->SendTo(&curbuf->data,curbuf->size,&dest);
	packetssent++;
	datasendthissec+=RELIABLEPACKETSIZE+1+16;
	datasendthisframe+=RELIABLEPACKETSIZE+1+16;
	
}

//prepares reliable data to be sent

void Sock::PackReliable()
{
	int curtimetillresend=0;
	rbuf *curbuf=0;
	for(int i=0;i<MAXSIMULRELIABLE;i++)
	{
		if(!reliablebuf[i].size)
		{
			curbuf=&reliablebuf[i];
			break;
		}
	}
	if(curbuf==0) return;	
sendmore:
	reliableintransmit++;
	int bufpos=0;
	bufpos+=4; //checksum
	WirePutU16(&curbuf->data.data[bufpos],reliablebufid);
	bufpos+=2;
	WirePutU16(&curbuf->data.data[bufpos],reliablebufnum);
	bufpos+=2;
	int thispacketsize=RELIABLEPACKETSIZE;
	if(reliablebufnum==0)
	{
		WirePutU32(&curbuf->data.data[bufpos],reliablesenddata->size);
		bufpos+=4;
		thispacketsize-=4;
	}
	if(reliablesenddata->size<=thispacketsize) 
	{
		thispacketsize=reliablesenddata->size;
		curbuf->data.type=LASTRELIABLE;
	}
	else
	{
		curbuf->data.type=RELIABLE;
	}
	CopyMemory(&curbuf->data.data[bufpos],reliablesenddata->data,thispacketsize);
	bufpos+=thispacketsize;
	WirePutU32(&curbuf->data.data[0],GetCheckSum(&curbuf->data.data[4],bufpos-4));
	MoveMemory(reliablesenddata->data,&reliablesenddata->data[thispacketsize],reliablesenddata->size-thispacketsize);
	reliablesenddata->size-=thispacketsize;
	curbuf->size=1+bufpos;
	curbuf->id=reliablebufid;
	curbuf->num=reliablebufnum;
	ResendReliable(curbuf);
	curbuf->resendat=GetTickCount()+RELIABLEFIRSTRESEND;
	curbuf->resends=0;
	if(reliablesenddata->size>RELIABLEPACKETSIZE)
	{
		curbuf=0;
		for(int i=0;i<MAXSIMULRELIABLE;i++)
		{
			if(!reliablebuf[i].size)
			{
				curbuf=&reliablebuf[i];
				break;
			}
		}
		if(curbuf==0) return;

		reliablebufnum++;
		goto sendmore;
	}

}
int Sock::SendReliable(const void *data,int size,int callback)
{
	
	if(data==0)
	{
		return !!reliablesenddata;
	}
	if(size<=0||size>BIGBUFSIZE-4) return 0;
	reliabledata **last=&reliablesenddata;
	int queued=0,queuedbytes=0;
	while(*last)
	{
		queued++;
		queuedbytes+=(*last)->size;
		last=&((*last)->next);
	}
	//bounded queue: a peer that stops acknowledging can't make us use unbounded memory
	if(queued>=MAXRELIABLEQUEUE||queuedbytes+size>MAXRELIABLEQUEUEBYTES)
	{
		reliablefailed=1;
		return 0;
	}
	*last=new reliabledata;
	(*last)->data=new char[size];
	(*last)->callback=callback;
	(*last)->size=size;
	(*last)->next=0;

	reliabledata *newdata=*last;
	CopyMemory(newdata->data,data,size);
	if(newdata==reliablesenddata)  //only one
	{
		reliablebufid++;
		reliablebufnum=0;
		PackReliable();
	}
	return 1;
}

void Sock::CheckReliableResend()
{
	if(!reliablesenddata) return;
	for(int i=0;i<MAXSIMULRELIABLE;i++)
	if(reliablebuf[i].size)
	{

		//real time, not frames; backs off, and gives up on a peer that never answers
		//(it would otherwise be flooded forever, which spoofed requests could aim at anyone)
		DWORD now=GetTickCount();
		if((int)(now-reliablebuf[i].resendat)<0) continue;
		if(reliablebuf[i].resends>=RELIABLEGIVEUP)
		{
			reliablefailed=1;
			continue;
		}
		ResendReliable(&reliablebuf[i]);
		reliablebuf[i].resends++;
		int delay=RELIABLEFIRSTRESEND<<(reliablebuf[i].resends<4?reliablebuf[i].resends:4);
		if(delay>RELIABLEMAXRESEND) delay=RELIABLEMAXRESEND;
		reliablebuf[i].resendat=now+delay;
	}
}
void Sock::ResetRecvReliable()
{
	lastreliableid=0;

}
void Sock::ResetReliable()
{
	lastreliableid=0;

	reliablebufid=0;
	reliableintransmit=0;
	reliablefailed=0;
	for(int i=0;i<MAXSIMULRELIABLE;i++)
	{
		reliablebuf[i].size=0;
	}
	while(reliablesenddata)
	{
		reliabledata *next=reliablesenddata->next;
		delete []reliablesenddata->data;
		delete reliablesenddata;
		reliablesenddata=next;
	}
}

//finds a checksum for reliable data

DWORD Sock::GetCheckSum(char *buf,int size)
{
	char checksum[4];
	checksum[0]=0;checksum[1]=0;checksum[2]=0;checksum[3]=0;
	for(int i=0;i<size;i++)
	{
		checksum[i&3]^=buf[i];
	}
	return WireGetU32(checksum);
}