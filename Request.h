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

#ifndef __REQUEST_H_
#define __REQUEST_H_

#include <pthread.h>
#include <time.h>

#include <vector>
#include <map>
#include <queue>

using std::vector;
using std::map;

#include "Mutex.h"
#include "Packet.h"
#include "Socket.h"

class NetworkRequest
{
public:
	static void Slice();
	
	static void HandleReceive( Socket *sock, PacketReader &reader );
	
	static void Register( int command, unsigned short reqID, int timeout = 10 );
	
	static bool WaitForResponse( unsigned short reqID );
	static PacketReader GetResponse( unsigned short reqID );
	
private:
	typedef map<unsigned short, NetworkRequest *> NetworkRequestMap;
	NetworkRequest();
	~NetworkRequest();
	
	int _Cmd;
	unsigned short _ID;
	queue<PacketReader> _Resp;
	pthread_mutex_t _Mutex;
	pthread_cond_t _Cond;
	timeval _EndTime;

	static vector<NetworkRequest *> _Delete;
	static NetworkRequestMap _ReqMap;
	static Mutex _GlobalMutex;
};

#endif
