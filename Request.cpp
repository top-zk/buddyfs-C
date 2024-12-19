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

#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <sched.h>

#include "Buddy.h"
#include "Packet.h"
#include "Socket.h"
#include "Request.h"

Mutex NetworkRequest::_GlobalMutex;
NetworkRequest::NetworkRequestMap NetworkRequest::_ReqMap;
vector<NetworkRequest*> NetworkRequest::_Delete;

NetworkRequest::NetworkRequest() : _Cmd( -1 ), _ID( 0 )
{
	pthread_cond_init( &_Cond, NULL );
	pthread_mutex_init( &_Mutex, NULL );
}

NetworkRequest::~NetworkRequest()
{
	pthread_cond_destroy( &_Cond );
	pthread_mutex_destroy( &_Mutex );
}

void NetworkRequest::Slice()
{
	vector<NetworkRequest *> bcast;
	
	_GlobalMutex.Lock();
	timeval now;
	
	while ( _Delete.size() > 0 )
	{
		delete _Delete[0];
		_Delete.erase( _Delete.begin() );
	}
	
	for( NetworkRequestMap::iterator iter = _ReqMap.begin(); iter != _ReqMap.end(); iter++ )
	{
		NetworkRequest *req = iter->second;
		
		gettimeofday( &now, NULL );
		
		if ( now.tv_sec > req->_EndTime.tv_sec || ( now.tv_sec == req->_EndTime.tv_sec && now.tv_usec >= req->_EndTime.tv_usec ) )
		{
			_ReqMap.erase( iter );
			_Delete.push_back( req );
			
			bcast.push_back( req );
		}
	}
	_GlobalMutex.Unlock();
	
	for(unsigned int i=0;i<bcast.size();i++)
	{
		NetworkRequest *req = bcast[i];
		
		pthread_mutex_lock( &req->_Mutex );
		pthread_cond_broadcast( &req->_Cond );
		pthread_mutex_unlock( &req->_Mutex );
			
		sched_yield();
	}
}

void NetworkRequest::HandleReceive( Socket *sock, PacketReader &reader )
{
	NetworkRequest *req = NULL;
	
	_GlobalMutex.Lock();
	NetworkRequestMap::iterator iter = _ReqMap.find( reader.RequestID() );
	if ( iter == _ReqMap.end() )
	{
		_GlobalMutex.Unlock();
		return;
	}
	_GlobalMutex.Unlock();

	req = iter->second;
		
	pthread_mutex_lock( &req->_Mutex );
		
	if ( req->_Cmd == reader.Command() )
	{
		req->_Resp.push( reader );
		pthread_cond_broadcast( &req->_Cond );
	}
		
	pthread_mutex_unlock( &req->_Mutex );
	
	sched_yield();
}

void NetworkRequest::Register( int command, unsigned short reqID, int timeout )
{
	NetworkRequest *req;
	
	_GlobalMutex.Lock();
	NetworkRequestMap::iterator iter = _ReqMap.find( reqID );
	if ( iter != _ReqMap.end() )
		req = iter->second;
	else
		req = new NetworkRequest();
	
	gettimeofday( &req->_EndTime, NULL );
	req->_EndTime.tv_sec += timeout;
	
	req->_Cmd = command;
	req->_ID = reqID;
	
	_ReqMap.insert( NetworkRequestMap::value_type( reqID, req ) );
	
	_GlobalMutex.Unlock();
}

bool NetworkRequest::WaitForResponse( unsigned short reqID )
{
	NetworkRequest *req = NULL;
	
	_GlobalMutex.Lock();
	NetworkRequestMap::iterator iter = _ReqMap.find( reqID );
	if ( iter != _ReqMap.end() )
		req = iter->second;
	_GlobalMutex.Unlock();
	
	if ( req == NULL )
		return false;
	
	pthread_mutex_lock( &req->_Mutex );
	if ( req->_Resp.empty() )
		pthread_cond_wait( &req->_Cond, &req->_Mutex );
	
	bool ok = !req->_Resp.empty();
	pthread_mutex_unlock( &req->_Mutex );
	
	return ok;
}

PacketReader NetworkRequest::GetResponse( unsigned short reqID )
{
	_GlobalMutex.Lock();
	NetworkRequestMap::iterator iter = _ReqMap.find( reqID );
	if ( iter == _ReqMap.end() )
	{
		_GlobalMutex.Unlock();
		return PacketReader( NULL );
	}
	_GlobalMutex.Unlock();
	
	// may regfault when no response or bad request id
	NetworkRequest *req = iter->second;
	
	pthread_mutex_lock( &req->_Mutex );
	
	if ( !req->_Resp.empty() )
	{
		PacketReader resp = req->_Resp.front();
		req->_Resp.pop();
	
		pthread_mutex_unlock( &req->_Mutex );
		
		return resp;
	}
	else
	{
		pthread_mutex_unlock( &req->_Mutex );
		
		return PacketReader( NULL );
	}
}


