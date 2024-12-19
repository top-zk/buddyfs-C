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

#ifndef __CLIQUE_H_
#define __CLIQUE_H_

// aka Group

#include <vector>

#include "Thread.h"

#include "Buddy.h"
#include "Mutex.h"
#include "Packet.h"
#include "Socket.h"

#define DATA_XFER_BLOCK 4096

// CAUTION: None of Clique's non-static operations are thread safe! You MUST Lock() and Unlock() the Clique when using it.
class Clique : public Mutex
{
public:
	static void Connected( Socket *sock );
	static bool HandleReceive( Socket *sock, PacketReader &reader );
	static void ChangeAddr( const NetAddress &from, const NetAddress &to );
	static void Disconnected( Socket *sock );
	
	Clique();
	virtual ~Clique();

	virtual void AddMember( const NetAddress &addr ); 
	void RemoveMember( const NetAddress &addr ); 
	
	virtual bool IsMember( const NetAddress &addr );
	
	AddressList Members();
	int NumberOfMembers();

	virtual int Broadcast( Packet &p );
	virtual bool SendOnce( Packet &p );

	virtual void OnConnect( Socket *sock );
	virtual bool OnReceive( Socket *sock, PacketReader &reader );
	virtual void OnDisconnect( Socket *sock );

protected:
	static std::vector<Clique *> _Cliques;
	static Mutex _GlobalMutex;
	
	AddressList _Members;
};

class AlphaClique : public Clique, public Thread
{
public:
	AlphaClique();
	virtual ~AlphaClique();

	void InitialStartup();
	bool ThisIsAlpha() const; // return true if we are an 'Alpha' node
	
	virtual bool SendOnce( Packet &p );
	virtual void AddMember( const NetAddress &addr ); 
	
	virtual void OnConnect( Socket *sock );
	virtual bool OnReceive( Socket *sock, PacketReader &reader );
	virtual void OnDisconnect( Socket *sock );

	virtual int ThreadMain();
	
private:
	bool _Initing, _IsAlpha;
	Socket *_Local;
};

class FileStorageClique : public Clique
{
public:
	FileStorageClique( File *file );
	~FileStorageClique();
	
	File *GetFile() { return _File; }
	
	void JoinClique( bool sync = false );
	
	int DataRequestID() const { return _DataID; }
	
	virtual bool OnReceive( Socket *sock, PacketReader &reader );
	
	void DownloadFrom( Socket *sock, int ver );
	void NoDownload();
	
private:
	File *_File;
	int _DataID;
};

#endif
