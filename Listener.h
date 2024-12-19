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

#ifndef __LISTENER_H_
#define __LISTENER_H_

#include <netinet/in.h>

class Listener
{
public:
	Listener();
	virtual ~Listener();

	bool Listen( unsigned short port );
	bool Slice( int u_sleep = 0 );
	void Close();

	int FD() const;

	void OnAccept( int sock, sockaddr_in addr );

private:
	int _Socket;
};

#endif
