/*
    BuddyFS - Peer2Peer Distributed File System
    Copyright (C) 2005  Rick Carback and Bryan Pass

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef __SOCKET_H_
#define __SOCKET_H_

#include <map>
#include <ostream>
#include <netinet/in.h>

#include "Buddy.h"
#include "Mutex.h"
#include "Packet.h"

#define SOCKET_BW_LIMIT 1024000 // 1 mb/s 

class NetAddress
{
public:
	static const NetAddress None() { return NetAddress( INADDR_NONE, 0xFFFF ); }
	
	explicit NetAddress( in_addr_t ip, unsigned short port ) : _Ip( ip ), _Port( port )
	{
	}
	
	bool operator == ( const NetAddress &addr ) const
	{
		return _Ip == addr._Ip && _Port == addr._Port;
	}
	
	bool operator != ( const NetAddress &addr ) const
	{
		return  _Ip != addr._Ip || _Port != addr._Port;
	}
	
	bool operator < ( const NetAddress &addr ) const
	{
		if ( _Ip == addr._Ip )
			return _Port < addr._Port;
		else
			return _Ip < addr._Ip;
	}
	
	bool operator > ( const NetAddress &addr ) const
	{
		if ( _Ip == addr._Ip )
			return _Port > addr._Port;
		else
			return _Ip > addr._Ip;
	}
	
	in_addr_t IP() const { return _Ip; }
	unsigned short Port() const { return _Port; }
	
	string IPToString() const
	{
		char temp[32];
		const unsigned char *ip = (const unsigned char*)&_Ip;
		
		sprintf( temp, "%d.%d.%d.%d", (int)ip[0], (int)ip[1], (int)ip[2], (int)ip[3] );
		
		return string( temp );
	}
	
	friend std::ostream &operator << ( std::ostream &out, const NetAddress &addr ) // haha friend functions. Benencassa can suck it
	{
		out << addr.IPToString() << ":" << addr._Port;
		return out;
	}
	
private:
	in_addr_t _Ip;
	unsigned short _Port;
};

class Socket : public Mutex
{
public:
	typedef std::map<int, Socket *> SocketMap;

	static void Slice( int u_sleep );
	static const NetAddress &LocalAddr() { return _LocalAddr; }

	Socket();
	virtual ~Socket();

	int FD() const { return _Socket; }
	virtual const NetAddress &Addr() const { return _Addr; }

	bool Connect( sockaddr_in addr, bool nonblocking = true );
	bool Connect( NetAddress na, bool nonblocking = true );
	void Detach();
	void Close();

	virtual void Attach( int handle, sockaddr_in addr );

	virtual void OnConnect();
	virtual void OnAccepted();
	virtual void Send( Packet &p );
	virtual bool OnReceive( PacketReader &reader );
	virtual void OnDisconnect();
	
private:
	bool DoRecv();
	bool DoSend();

	static SocketMap _Map;
	static Mutex _GlobalMutex;
	static NetAddress _LocalAddr;

	NetAddress _Addr;
	char *_SendBuff;
	char *_RecvBuff;
	int _SBLen, _RBLen;
	int _SBEnd;
	int _SBPos, _RBPos;
	
	int _BytesThisSec;

	int _Socket;
	int _ThisSec;

	bool _Connecting;
};

class LoopbackSocket : public Socket
{
public:
	static Socket *Instance()
	{
		if ( _Inst == NULL )
			_Inst = new LoopbackSocket();
		
		return _Inst;
	}
	
	virtual ~LoopbackSocket(){}
	
	virtual void Attach( int handle, sockaddr_in addr )	{}
	virtual void OnConnect(){ Close(); }
	virtual void OnDisconnect(){}
	
	virtual const NetAddress &Addr() const { return Socket::LocalAddr(); }

	virtual void Send( Packet &p )
	{
		PacketReader reader = p.MakeReader();
		OnReceive( reader );
	}
	
private:
	LoopbackSocket()
	{
	}
	
	static LoopbackSocket *_Inst;
};

#endif
