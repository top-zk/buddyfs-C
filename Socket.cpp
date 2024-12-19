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

#include <map>
#include <iostream>
using namespace std;

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>

#include "Request.h"
#include "Buddy.h"
#include "Mutex.h"
#include "Socket.h"
#include "Packet.h"

Socket::SocketMap Socket::_Map;
Mutex Socket::_GlobalMutex;
NetAddress Socket::_LocalAddr = NetAddress::None();

LoopbackSocket *LoopbackSocket::_Inst = NULL;

void Socket::Slice( int u_timeout )
{
	fd_set read, write, except;
	timeval timeout;

	timeout.tv_sec = 0;
	timeout.tv_usec = u_timeout;

	FD_ZERO( &read );
	FD_ZERO( &write );
	FD_ZERO( &except );

	int maxFD = 0;
	SocketMap::iterator iter;

	_GlobalMutex.Lock();
	
	for ( iter = _Map.begin() ; iter != _Map.end(); iter++ )
	{
		int fd = iter->first;
		FD_SET( fd, &read );
		FD_SET( fd, &except );
		
		if ( iter->second->_SBPos < iter->second->_SBEnd || iter->second->_Connecting )
			FD_SET( fd, &write );

		if ( fd > maxFD )
			maxFD = fd;
	}

	_GlobalMutex.Unlock();
	
	if ( !maxFD )
		return;
	
	if ( _LocalAddr == NetAddress::None() )
	{
		sockaddr_in sai;
		socklen_t len = sizeof(sockaddr_in);
		if ( getsockname( maxFD, (sockaddr*)&sai, &len ) == 0 )
		{
			_LocalAddr = NetAddress( sai.sin_addr.s_addr, LocalPort );
			
			Clique::ChangeAddr( NetAddress::None(), _LocalAddr );
		}
	}

	int res = select( maxFD + 1, &read, &write, &except, &timeout );

	if ( res < 0 )
	{
		cout << "Socket::Slice() select error " << errno << ": " << strerror( errno ) << endl;
		return ;
	}

	if ( res == 0 )
		return ;

	_GlobalMutex.Lock();
	
	for ( iter = _Map.begin(); res > 0 && iter != _Map.end(); iter++ )
	{
		_GlobalMutex.Unlock();
		
		Socket *sock = iter->second;
		int fd = iter->first;
		
		bool remove = false;
		
		if ( FD_ISSET( fd, &except ) )
		{
			res--;
			
			remove = true;
		}

		if ( FD_ISSET( fd, &read ) )
		{
			res--;
			
			if ( !remove )
				remove = !sock->DoRecv();
		}

		if ( FD_ISSET( fd, &write ) )
		{
			res--;
			
			if ( !remove )
				remove = !sock->DoSend();
		}

		if ( remove )
		{
			sock->OnDisconnect();
			sock->Close();
			
			delete sock;
		}

		_GlobalMutex.Lock();
	}

	_GlobalMutex.Unlock();
}

Socket::Socket()
	: _Addr( NetAddress::None() ), _SendBuff( NULL ), _RecvBuff( NULL ), _SBLen( 0 ), _RBLen( 0 ), _SBEnd( 0 ), _SBPos( 0 ), _RBPos( 0 ), _Socket( 0 ), _Connecting( false )
{
	_BytesThisSec = 0;
	_ThisSec = 0;
}

Socket::~Socket()
{
	Close();

	delete[] _SendBuff;
	delete[] _RecvBuff;
}

void Socket::Attach( int handle, sockaddr_in addr )
{
	Close();
	
	_GlobalMutex.Lock();
	
	_Socket = handle;
	_Addr = NetAddress( addr.sin_addr.s_addr, ntohs( addr.sin_port ) );
	
	PeerMap::iterator iter = Peers.find( _Addr );
	if ( iter != Peers.end() )
		Peers.erase( iter );
	Peers.insert( Peer( _Addr, this ) );

	_Map.insert( SocketMap::value_type( _Socket, this ) );

	int on = 1;
	ioctl( _Socket, FIONBIO, &on );
	
	_GlobalMutex.Unlock();
}

bool Socket::Connect( NetAddress na, bool nonblocking )
{
	sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = htons( na.Port() );
	addr.sin_addr.s_addr = na.IP();

	return Connect( addr, nonblocking );
}

bool Socket::Connect( sockaddr_in addr, bool nonblocking )
{
	int on = 1;
	
	Close();
	
	_Socket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if ( _Socket <= 0 )
	{
		_Socket = 0;
		return false;
	}

	_Addr = NetAddress( addr.sin_addr.s_addr, ntohs( addr.sin_port ) );

	_Connecting = true;

	_Map.insert( SocketMap::value_type( _Socket, this ) );
	
	cout << "Connecting to " << _Addr << "..." << endl;

	if ( nonblocking )
	{
		ioctl( _Socket, FIONBIO, &on );

		return connect( _Socket, (sockaddr*)&addr, sizeof(sockaddr_in) ) == -1 && ( errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK );
	}
	else
	{
		if ( connect( _Socket, (sockaddr*)&addr, sizeof(sockaddr_in) ) )
		{
			cout << "Failed to connect to " << _Addr << endl;
			Close();
			return false;
		}
		else
		{
			ioctl( _Socket, FIONBIO, &on );
			
			PeerMap::iterator iter = Peers.find( _Addr );
			if ( iter != Peers.end() )
				Peers.erase ( iter );
			Peers.insert( Peer( _Addr, this ) );
			
			OnConnect();
			
			return true;
		}
	}
}

void Socket::Detach()
{
	if ( _Socket )
	{
		_GlobalMutex.Lock();
		
		SocketMap::iterator it = _Map.find( _Socket );
		if ( it != _Map.end() )
			_Map.erase( it );

		_GlobalMutex.Unlock();
		
		PeerMap::iterator iter = Peers.find( _Addr );
		if ( iter != Peers.end() )
			Peers.erase( iter );

		_Socket = 0;
	}
}

void Socket::Close()
{
	if ( _Socket )
		close( _Socket );
	Detach();
}

bool Socket::DoRecv()
{
	int val, toGet;
	
	if ( !_Socket )
		return false;

	do
	{
		if ( !_RecvBuff )
		{
			char buff[5];
			
			val = recv( _Socket, buff, 5, MSG_PEEK );

			if ( ( val > 0 && val < 5 ) || ( val == -1 && ( errno == EAGAIN || errno == EWOULDBLOCK ) ) )
				return true;
			else if ( val < 5 )
				return false;
			
			_RBPos = 0;
			_RBLen = ntohl( *(unsigned int*)&buff[1] );
			
			if ( _RBLen <= 0 || _RBLen > 65536 )
				return false; // invalid packet

			_RecvBuff = new char[_RBLen];
		}
		
		toGet = _RBLen - _RBPos;
		
		val = recv( _Socket, &_RecvBuff[_RBPos], toGet, 0 );
		
		if ( val > 0 )
		{
			_RBPos += val;

			if ( _RBPos == _RBLen )
			{
				PacketReader reader( _RecvBuff );
				if ( !OnReceive( reader ) )
					return false;

				delete[] _RecvBuff;
				_RecvBuff = NULL;
				_RBLen = 0;
			}
		}
		else if ( val == 0 || ( errno != EAGAIN && errno != EWOULDBLOCK ) )
		{
			return false;
		}
	} while ( val == toGet );

	return true;
}

bool Socket::DoSend()
{
	Lock();

	if ( _Connecting )
	{
		_Connecting = false;

		Unlock();
		
		PeerMap::iterator iter = Peers.find( _Addr );
		if ( iter != Peers.end() )
			Peers.erase( iter );
		Peers.insert( Peer( _Addr, this ) );
		
		OnConnect();
		
		return true;
	}
	
	if ( time(NULL) != _ThisSec )
	{
		if ( _BytesThisSec >= 256 )
			cout << Addr() << ": Out bandwidth " << int( (double(_BytesThisSec)/1024.0) * 10 )/10.0 << " KB/s" << endl;
		
		_BytesThisSec = 0;
		_ThisSec = time(NULL);
	}
	
	if ( _BytesThisSec < SOCKET_BW_LIMIT )
	{		
		int left = _SBEnd - _SBPos;
		while ( left > 0 && _BytesThisSec < SOCKET_BW_LIMIT )
		{
			int s = send( _Socket, &_SendBuff[ _SBPos ], left, 0 );
					
			if ( s > 0 )
			{
				_SBPos += s;
				_BytesThisSec += s;
				left -= s;
			}
			else
			{
				if ( errno != EAGAIN && errno != EWOULDBLOCK )
				{
					Unlock();
					return false;
				}
				
				break;
			}
		}
	
		if ( _SBPos >= _SBEnd )
			_SBPos = _SBEnd = 0;
	}
	
	Unlock();
	
	return true;
}

void Socket::Send( Packet & p )
{
	if ( _Connecting )
		return;
	
	int len = p.Length();
	const char *data = p.Buffer();

	Lock();
	
	int newSize = ( _SBEnd - _SBPos ) + len;

	if ( _SBLen > newSize && _SBEnd + len > _SBLen )
	{
		memmove( _SendBuff, &_SendBuff[ _SBPos ], _SBEnd - _SBPos );
		_SBEnd -= _SBPos;
		_SBPos = 0;
	}
	else if ( _SBLen < newSize )
	{
		char * temp = _SendBuff;
		_SendBuff = new char[ newSize ];
		memcpy( _SendBuff, &temp[ _SBPos ], _SBEnd - _SBPos );
		delete[] temp;

		_SBEnd -= _SBPos;
		_SBPos = 0;
		_SBLen = newSize;
	}

	memcpy( &_SendBuff[ _SBEnd ], data, len );
	_SBEnd += len;

	Unlock();
}

void Socket::OnConnect()
{
	_Connecting = false;
	
	cout << _Addr << ": Connected." << endl;
	
	Clique::Connected( this );
	
	Packet p( IN_PORT );
	p.WriteShort( LocalPort );
	Send( p );
}

void Socket::OnAccepted()
{
	cout << _Addr << ": Incoming connection established." << endl;
	
	Clique::Connected( this );
}

bool Socket::OnReceive( PacketReader &reader )
{
	cout << Addr() << ": Recv " << reader << endl;
	
	reader.Seek( PacketReader::PAYLOAD_BEGIN );
	
	switch( reader.Command() )
	{
		// this ensure we identify all connections from this client with the same port number,rather than random local port confusion.
		case IN_PORT:
		{
			unsigned short port = (unsigned short)reader.ReadShort();
			
			if ( port != _Addr.Port() )
			{
				PeerMap::iterator iter = Peers.find( _Addr );
				if ( iter != Peers.end() )
					Peers.erase( iter );
				
				NetAddress old = _Addr;
				_Addr = NetAddress( _Addr.IP(), port );
				
				Clique::ChangeAddr( old, _Addr );
				Peers.insert( Peer( _Addr, this ) );
			}
			
			break;
		}
		
		default:
		{
			NetworkRequest::HandleReceive( this, reader );
			Clique::HandleReceive( this, reader );
	
			break;
		}
	}
	
	return true;
}

void Socket::OnDisconnect()
{
	_Connecting = false;
	
	Clique::Disconnected( this );
	
	cout << Addr() << ": Disconnected! " << endl;
}
