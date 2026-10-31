//Sock is a class that makes use of windows sockets, uses udp.  It can be used to send 
//anything needed, and can also send data reliably in case of packetloss.  The reliable
//transfer can be used to send large amounts of data also.  If many small packets are being
//sent, it can group them together to save bandwidth.



#include <stdafx.h>
#include "multiplay.h"
#include "sock.h"
#include "redrawing.h"

#define RELIABLE -1
#define LASTRELIABLE -2
#define ACK -3

#define RELIABLEPACKETSIZE (int)450

int datasendthissec;
int datarecvthissec;
int datasendthisframe;
int datarecvthisframe;


Sock::Sock()
{
	ZeroMemory(this,sizeof(this));
	dest.sin_family=2;
	reliablesenddata=0;
}

//sends all data waiting to be sent

void Sock::Flush()
{
	sendbuf.type=0;
	if(!sendbufpos) return;
	int sent=sendto(sock,(char *)&sendbuf,sendbufpos+1,0,(SOCKADDR*)&dest,sizeof(dest));
	packetssent++;
	datasendthissec+=sendbufpos+1+16;
	datasendthisframe+=sendbufpos+1+16;
	sendbufpos=0;
}

int Sock::Send(void *data,int size)
{
	if(size+sendbufpos>MAXPACKETSIZE) Flush();
	WORD wsize=size;
	CopyMemory(&sendbuf.data[sendbufpos],&wsize,sizeof(wsize));
	sendbufpos+=sizeof(wsize);
	CopyMemory(&sendbuf.data[sendbufpos],data,size);
	sendbufpos+=size;
	return 1;
}

//checks for data received by the socket, decides weather they are reliable, or are a groups of packets.
//it returns 1 if there are more packets that can be read (by calling Receive again)

int Sock::Receive(void *data,SOCKADDR_IN *source)
{
	if(!sock) return 0;
	if(reliablerecvbufpos<reliablerecvbufsize)
	{
		DWORD dwsize=*(DWORD*)&reliablerecvdata[reliablerecvbufpos];
		reliablerecvbufpos+=4;
		CopyMemory(data,&reliablerecvdata[reliablerecvbufpos],dwsize);
		reliablerecvbufpos+=dwsize;
		return dwsize;
	}
	if(recvbufpos>=recvbufsize)
	{
beginning:
		fd_set recv;
		recv.fd_count=1;
		recv.fd_array[0]=sock;
		timeval tm;
		tm.tv_sec=0;
		tm.tv_usec=0;
		int readit;
		readit=select(0,&recv,0,0,&tm);
		if(!readit) return 0;
		int addrsize=sizeof(SOCKADDR_IN);
		recvbufsize=recvfrom(sock,(char *)&recvbuf,sizeof(recvbuf),0,(SOCKADDR*)source,&addrsize)-1;
		packetsreceived++;
		if(recvbufsize<=0) return 0;
		datarecvthissec+=recvbufsize+16;
		datarecvthisframe+=recvbufsize+16;
		int recvedreldata=0;
		if(recvbuf.type==ACK)
		{
			for(int i=0;i<MAXSIMULRELIABLE;i++)
			{
				if(reliablebuf[i].size&&reliablebuf[i].id==*(WORD*)&recvbuf.data[0]&&reliablebuf[i].num==*(WORD*)&recvbuf.data[2]&&reliablesenddata)
				{
						
					reliablebuf[i].size=0;
					reliableintransmit--;
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
						SimpleMessage callback;
						callback.type=SIMPLE;
						callback.type2=0;
						callback.msg=0;
						callback.msg2=0;
						CopyMemory(data,&callback,sizeof(callback));
						recvedreldata=1;
						return sizeof(callback);
					}
					else
					{
						SimpleMessage callback;
						callback.type=SIMPLE;
						callback.type2=reliablesenddata->callback;
						callback.msg=0;
						callback.msg2=0;
						CopyMemory(data,&callback,sizeof(callback));
						
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
						recvbufpos=recvbufsize;
						return sizeof(callback);
					}
					break;
				}
			}
			goto beginning;
		}
		if(recvbuf.type==RELIABLE||recvbuf.type==LASTRELIABLE)
		{
			CopyMemory(&dest,source,sizeof(dest));
			recvbufpos=recvbufsize;
			if(GetCheckSum(&recvbuf.data[4],recvbufsize-4)!=*(DWORD *)recvbuf.data)
			{
				*(char *)data=0;
				return 1;
			}
			buf ack;
			ack.type=ACK;
			*(WORD*)&ack.data[0]=*(WORD*)&recvbuf.data[4];
			*(WORD*)&ack.data[2]=*(WORD*)&recvbuf.data[6];
			sendto(sock,(char *)&ack,1+4,0,(SOCKADDR *)&dest,sizeof(SOCKADDR));
			packetssent++;
			if(*(WORD *)&recvbuf.data[4]!=lastreliableid+1)
			{
				*(char *)data=0;
				return 1;
			}
			if(recvbuf.type==LASTRELIABLE)
			{
				lastreliableid=*(WORD*)&recvbuf.data[4];
				
			}
			int rbufpos=*(WORD*)&recvbuf.data[6]*RELIABLEPACKETSIZE;
			CopyMemory(&reliablerecvdata[rbufpos],&recvbuf.data[8],recvbufsize-8);
			if(recvbuf.type==LASTRELIABLE)
			{
				reliablerecvbufpos=0;
				DWORD dwsize=*(DWORD*)&reliablerecvdata[reliablerecvbufpos];
				reliablerecvbufpos+=4;
				CopyMemory(data,&reliablerecvdata[reliablerecvbufpos],dwsize);
				reliablerecvbufpos+=dwsize;
				reliablerecvbufsize=rbufpos+recvbufsize-8;
				return dwsize;
			}
			else
			{
				*(char *)data=0;
				recvbufpos=recvbufsize;
				return 1;
			}
		}
		recvbufpos=0;
	}
	WORD wsize;
	CopyMemory(&wsize,&recvbuf.data[recvbufpos],sizeof(wsize));
	recvbufpos+=sizeof(wsize);
	CopyMemory(data,&recvbuf.data[recvbufpos],wsize);
	recvbufpos+=wsize;
	return wsize;
}

//used internally

void Sock::ResendReliable(rbuf *curbuf)
{
	sendto(sock,(char *)&curbuf->data,curbuf->size,0,(SOCKADDR*)&dest,sizeof(dest));
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
	*(WORD *)&curbuf->data.data[bufpos]=reliablebufid;
	bufpos+=2;
	*(WORD *)&curbuf->data.data[bufpos]=reliablebufnum;
	bufpos+=2;
	int thispacketsize=RELIABLEPACKETSIZE;
	if(reliablebufnum==0)
	{
		*(DWORD *)&curbuf->data.data[bufpos]=reliablesenddata->size;
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
	*(DWORD *)&curbuf->data.data[0]=GetCheckSum(&curbuf->data.data[4],bufpos-4);
	MoveMemory(reliablesenddata->data,&reliablesenddata->data[thispacketsize],reliablesenddata->size-thispacketsize);
	reliablesenddata->size-=thispacketsize;
	curbuf->size=1+bufpos;
	curbuf->id=reliablebufid;
	curbuf->num=reliablebufnum;
	ResendReliable(curbuf);
	curbuf->timetillresend=10;
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
int Sock::SendReliable(void *data,int size,int callback)
{
	
	if(data==0)
	{
		return !!reliablesenddata;
	}
	reliabledata **last=&reliablesenddata;
	while(*last)
	{
		last=&((*last)->next);
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

		reliablebuf[i].timetillresend--;
		if(reliablebuf[i].timetillresend<0)
		{
			ResendReliable(&reliablebuf[i]);
			reliablebuf[i].timetillresend=40;
		}
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
	return *(DWORD *)checksum;
}