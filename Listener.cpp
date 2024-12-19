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

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "Listener.h"
#include "Socket.h"

Listener::Listener() : _Socket( 0 )
{}

Listener::~Listener()
{
	Close();
}

bool Listener::Listen( unsigned short port )
{
	sockaddr_in addr;
	if ( _Socket )
		Close();

	_Socket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if ( _Socket <= 0 )
	{
		_Socket = 0;
		return false;
	}

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( port );

	if ( bind( _Socket, (sockaddr *)&addr, sizeof(sockaddr_in) ) )
	{
		Close();
		return false;
	}

	if ( listen( _Socket, 50 ) )
	{
		Close();
		return false;
	}

	return true;
}

bool Listener::Slice( int u_timeout )
{
	int val;
	
	do {
		fd_set read, except;
		timeval timeout;

		timeout.tv_sec = 0;
		timeout.tv_usec = u_timeout;

		FD_ZERO( &read );
		FD_ZERO( &except );

		FD_SET( _Socket, &read );
		FD_SET( _Socket, &except );
		
		val = select( _Socket + 1, &read, NULL, &except, &timeout );
		
		if ( val < 0 )
		{
			Close();
			return false;
		}
	
		if ( val > 0 )
		{
			if ( FD_ISSET( _Socket, &except ) )
				return false;
	
			if ( FD_ISSET( _Socket, &read ) )
			{
				sockaddr_in addr;
				socklen_t len = sizeof( sockaddr_in );
				int newSock = accept( _Socket, (sockaddr*)&addr, &len );
	
				if ( newSock > 0 )
					OnAccept( newSock, addr );
				else
					cout << "Error on accept: " << strerror(errno) << endl;
			}
		}
	} while ( val > 0 );

	return true;
}

void Listener::Close()
{
	if ( _Socket > 0 )
		close( _Socket );
	_Socket = 0;
}

int Listener::FD() const
{
	return _Socket;
}

void Listener::OnAccept( int sock, sockaddr_in addr )
{
	Socket *s = new Socket();
	s->Attach( sock, addr );
	s->OnAccepted();
}
