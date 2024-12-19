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

#include <vector>

using std::vector;

#include <pthread.h>
#include <dirent.h>
#include <fcntl.h>

#include "Buddy.h"
#include "Socket.h"
#include "Clique.h"
#include "FileSystem.h"

#include "drm.h"

vector<Clique *> Clique::_Cliques;
Mutex Clique::_GlobalMutex;

void Clique::Connected( Socket *sock )
{
	_GlobalMutex.Lock();

	for ( vector<Clique*>::iterator iter = _Cliques.begin(); iter != _Cliques.end(); iter++ )
		(*iter)->OnConnect( sock );
	
	_GlobalMutex.Unlock();
}

bool Clique::HandleReceive( Socket *sock, PacketReader &reader )
{
	_GlobalMutex.Lock();
	vector<Clique*> cliques = _Cliques;
	_GlobalMutex.Unlock();
	
	for ( vector<Clique*>::iterator iter = cliques.begin(); iter != cliques.end(); iter++ )
	{
		Clique *c = *iter;

		reader.Seek( PacketReader::PAYLOAD_BEGIN ); // back to the begining
			
		if ( c->OnReceive( sock, reader ) )
			return true;
	}

	return false;
}

void Clique::ChangeAddr( const NetAddress &from, const NetAddress &to )
{
	_GlobalMutex.Lock();
	for ( vector<Clique*>::iterator iter = _Cliques.begin(); iter != _Cliques.end(); iter++ )
	{
		Clique *c = *iter;

		c->Lock();
		for( AddressList::iterator iter = c->_Members.begin(); iter != c->_Members.end(); iter++ )
		{
			if ( *iter == from )
				*iter = to;
		}
		c->Unlock();
	}
	_GlobalMutex.Unlock();
}

void Clique::Disconnected( Socket *sock )
{
	_GlobalMutex.Lock();
	vector<Clique*> cliques = _Cliques;
	_GlobalMutex.Unlock();
	
	for ( vector<Clique*>::iterator iter = cliques.begin(); iter != cliques.end(); iter++ )
		(*iter)->OnDisconnect( sock );
}

Clique::Clique()
{
	_GlobalMutex.Lock();

	_Cliques.push_back( this );
	
	_GlobalMutex.Unlock();
}

Clique::~Clique()
{
	_GlobalMutex.Lock();

	for(unsigned int i=0;i<_Cliques.size();i++)
	{
		if ( _Cliques[i] == this )
		{
			_Cliques.erase( _Cliques.begin() + i );
			break;
		}
	}
	
	_GlobalMutex.Unlock();
}

void Clique::AddMember( const NetAddress &addr )
{
	if ( !IsMember( addr ) )
	{
		Lock();
		_Members.push_back( addr );
		Unlock();
	}
}

void Clique::RemoveMember( const NetAddress &addr )
{
	Lock();
	for(AddressList::iterator iter = _Members.begin(); iter != _Members.end(); iter++)
	{
		if ( *iter == addr )
		{
			_Members.erase( iter );
			break;
		}
	}
	Unlock();
}

bool Clique::IsMember( const NetAddress &addr )
{
	Lock();
	for(AddressList::iterator iter = _Members.begin(); iter != _Members.end(); iter++)
	{
		if ( *iter == addr )
		{
			Unlock();
			return true;
		}
	}
	Unlock();
	return false;
}

int Clique::Broadcast( Packet &p )
{
	int count = 0;

	Lock();
	for(AddressList::iterator iter = _Members.begin(); iter != _Members.end(); iter++)
	{
		Unlock();
		
		Socket *sock = FindPeer( *iter );
		
		if ( sock )
		{
			sock->Send( p );
			count++;
		}
		
		Lock();
	}
	Unlock();
	return count;
}

bool Clique::SendOnce( Packet &p )
{
	bool sent = false;
	
	Lock();
	
	for(AddressList::iterator iter = _Members.begin(); iter != _Members.end() && !sent; iter++)
	{
		Unlock();
		
		Socket *sock = FindPeer( *iter );
		
		if ( sock )
		{
			sock->Send( p );
			sent = true;
		}
		
		Lock();
	}
	
	Unlock();

	return sent;
}

void Clique::OnConnect( Socket *sock )
{
}

bool Clique::OnReceive( Socket *sock, PacketReader &reader )
{
	return false;
}

void Clique::OnDisconnect( Socket *sock )
{
	RemoveMember( sock->Addr() );
}

AddressList Clique::Members()
{
	Lock();
	AddressList ret = _Members;
	Unlock();
	
	return ret;
}

int Clique::NumberOfMembers()
{
	Lock();
	int size = _Members.size();
	Unlock();
	
	return size;
}



AlphaClique::AlphaClique() : _Initing( false ), _IsAlpha( true ), _Local( FindPeer( Socket::LocalAddr() ) )
{
}

AlphaClique::~AlphaClique()
{
}

void AlphaClique::InitialStartup()
{
	StartThread();

	if ( JoinThread() == 0 )
		_IsAlpha = false;
}

bool AlphaClique::SendOnce( Packet &p )
{
	if ( _Initing )
		JoinThread();
	
	if ( ThisIsAlpha() )
	{
		_Local->Send( p );
		return true;
	}
	else
	{
		if ( !Clique::SendOnce( p ) )
		{
			_IsAlpha = true;
			_Local->Send( p );
		}
		
		return true;
	}
}

bool AlphaClique::ThisIsAlpha() const // return true if we are an 'Alpha' node
{
	return _IsAlpha;
}

int AlphaClique::ThreadMain()
{
	_Initing = true;
	Lock();

	Socket *newSock = new Socket();
	for(AddressList::iterator iter = _Members.begin(); iter != _Members.end(); iter++)
	{
		Unlock();
		if ( FindPeer( *iter ) != NULL )
		{
			_Initing = false;
			return 0;
		}

		if ( newSock->Connect( *iter, false ) )
		{
			Packet p( HANDSHAKE );
			
			newSock->Send( p );
			
			_Initing = false;
			return 0;
		}
		Lock();
	}

	delete newSock;

	Unlock();
	_Initing = false;
	
	return 1;
}

void AlphaClique::AddMember( const NetAddress &addr )
{
	if (addr != Socket::LocalAddr())
	{
		Clique::AddMember(addr);
	}
}


void AlphaClique::OnConnect( Socket *sock )
{
	if ( ThisIsAlpha() )
	{		
		if ( Peers.size() > 15 )
		{
			Packet p( MAKE_ALPHA );
			FileSystem::WriteFullList( p );
			sock->Send( p );
			AddMember(sock->Addr());
		}
	}
}

void AlphaClique::OnDisconnect( Socket *sock )
{
	if ( IsMember( sock->Addr() ) )
	{
		if ( !ThisIsAlpha() )
		{
			if ( !_Initing )
				StartThread();
		}
		else
		{
			for ( PeerMap::iterator iter = Peers.begin(); iter != Peers.end(); iter++ )
			{
				if ( iter->second != NULL && iter->second != sock && !IsMember( iter->first ) )
				{
					Packet p( MAKE_ALPHA );
					FileSystem::WriteFullList( p );
					iter->second->Send( p );
					
					AddMember( iter->first );
					
					break;
				}
			}
		}
		
		Clique::OnDisconnect( sock );
	}
}

bool AlphaClique::OnReceive( Socket *sock, PacketReader &reader )
{
	switch ( reader.Command() )
	{
		case MAKE_ALPHA:
		{
			AddMember( sock->Addr() );
			Lock();
			_IsAlpha = true;
			for( AddressList::iterator iter = _Members.begin(); iter != _Members.end(); iter++ )
			{
				if ( FindPeer( *iter ) == NULL )
					(new Socket())->Connect( *iter );
			}
			Unlock();
			
			while ( !reader.AtEnd() )
			{
				char name[MAX_PATH];
				int type;

				reader.ReadASCII( name, MAX_PATH );
				type = reader.ReadByte();

				mode_t mode = reader.ReadUnsignedInt();
				time_t mtime = reader.ReadUnsignedInt();
				time_t ctime = reader.ReadUnsignedInt();

				off_t size = 0;
				AddressList list;

				if ( type == DT_REG )
				{
					size = reader.ReadUnsignedInt();

					int c = reader.ReadInt();
					for(int j=0;j<c;j++)
						list.push_back( reader.ReadAddress() );
				}

				FSObject *obj = FileSystem::AddObject( name, type );
				if ( !obj )
					obj = FileSystem::GetObject( name );

				if ( !obj )
					continue;

				obj->Mode( mode );
				obj->mTime( mtime );
				obj->cTime( ctime );

				if ( obj->IsFile() )
				{
					File *file = (File*)obj;

					file->Size( size );

					for ( AddressList::iterator iter = list.begin(); iter != list.end(); iter++ )
						file->GetClique()->AddMember( *iter );
				}
			}
			
			return true;	
		}
		
		case HANDSHAKE:
		{
			int count = 0;
			
			Packet p( HANDSHAKE_RESP );
						
			p.WriteShort( _Members.size() );
			
			p.WriteBool( ThisIsAlpha() );
			
			Lock();
			for(AddressList::iterator iter = _Members.begin(); iter != _Members.end(); iter++, count++)
				p.WriteAddress( *iter );
			Unlock();
			
			sock->Send( p );
			
			
			return true;
		}
		
		case HANDSHAKE_RESP:
		{
			int count = reader.ReadShort();
			bool isAlpha = reader.ReadBool();
			
			for(int i=0;i<count;i++)
				AddMember( reader.ReadAddress() );
			
			if ( !isAlpha ) // if they aren't an alpha then try from the list they gave us
			{
				sock->Close();
				
				if ( !_Initing )
					StartThread();
			}
			else
			{
				list<string> localFiles;
				FileSystem::BuildList( localFiles );
				
				Packet p( LOCAL_FILES );
				
				p.WriteInt( localFiles.size() );
				
				for( list<string>::iterator iter=localFiles.begin(); iter != localFiles.end(); iter ++ )
					p.WriteASCII( iter->c_str() );
				
				sock->Send( p );
				AddMember( sock->Addr() );
			}
			
			return true;
		}
		
		case LOCAL_FILES:
		{
			if ( !ThisIsAlpha() )
				return false;
			
			int count = reader.ReadInt();
			
			for(int i=0;i<count;i++)
			{
				char path[MAX_PATH];
				
				reader.ReadASCII( path, MAX_PATH );
				
				File *file = (File*)FileSystem::AddObject( path, DT_REG, true );
				if ( !file )
					file = (File*)FileSystem::GetObject( path );
				
				if ( !file || !file->IsFile() )
					continue;
				
				file->GetClique()->AddMember( sock->Addr() );
			}
			
			return true;
		}
		
		case LIST_REQ:
		{
			char path[MAX_PATH];
			Packet p( LIST_RESP, reader.RequestID() );
			
			reader.ReadASCII( path, MAX_PATH );
			
			Folder *folder = (Folder*)FileSystem::GetObject( path );
			if ( !folder )
			{
				p.WriteShort( -ENOENT );
			}
			else if ( folder->Type() != DT_DIR )
			{
				p.WriteShort( -ENOTDIR );
			}
			else
			{
				FSList files = folder->GetList();

				p.WriteShort( files.size() );

				for( FSList::iterator iter = files.begin(); iter != files.end(); iter++ )
				{
					FSObject *obj = *iter;
					p.WriteASCII( obj->Name() );
					p.WriteByte( obj->Type() );
					p.WriteUnsignedInt( obj->Mode() );
					p.WriteUnsignedInt( obj->mTime() );
					p.WriteUnsignedInt( obj->cTime() );
				
					if ( obj->IsFile() )
					{
						File *file = (File*)obj;

						p.WriteUnsignedInt( file->Size() );
						
						AddressList list = file->GetClique()->Members();
						
						p.WriteInt( list.size() );
						
						for(AddressList::const_iterator iter = list.begin(); iter != list.end(); iter++)
							p.WriteAddress( *iter );
					}
				}
			}
			
			sock->Send( p );
			
			return true;
		}
		
		case CREATE_REQ:
		{
			char path[MAX_PATH];
			FSObject *newObj;
			NetAddress addr = reader.ReadAddress();
			int type = (unsigned)reader.ReadByte();
			mode_t mode = reader.ReadUnsignedInt();
			short count = 0;
			
			reader.ReadASCII( path, MAX_PATH );
			
			if ( !IsMember( sock->Addr() ) )
			{
				Packet p = reader.MakePacket();
				count = (short)Broadcast( p );
			}
			
			Packet p( CREATE_RESP, reader.RequestID() );
			p.WriteShort( count );
			
			newObj = FileSystem::AddObject( path, type );
			if ( newObj == NULL )
			{
				p.WriteInt( EEXIST );
				return true;
			}
			else
			{
				p.WriteInt( 0 );
			}
			
			newObj->Mode( mode );
			
			if ( newObj->IsFile() )
				((File*)newObj)->GetClique()->AddMember( sock->Addr() );
			
			sock->Send( p );
			
			return true;
		}
		
		case CREATE_RESP:
		{
			NetAddress addr = reader.ReadAddress();
			
			if ( addr != Socket::LocalAddr() )
			{
				PeerMap::iterator iter = Peers.find( addr );
				
				if ( iter != Peers.end() )
				{
					Packet p = reader.MakePacket();
					iter->second->Send( p );
					return true;
				}
			}
			
			return false;
		}
		
		case FS_REQ:
		{
			char path[MAX_PATH];
			Packet p( FS_RESP, reader.RequestID() );
			
			reader.ReadASCII( path, MAX_PATH );
			
			FSObject *obj = FileSystem::GetObject( path );
			
			if ( !obj )
			{
				p.WriteShort( -ENOENT );
			}
			else 
			{
				p.WriteShort( 1 );
			
				p.WriteASCII( path );
				
				//Type
				p.WriteByte( obj->Type() );
				//Mode (Unsigned Integer)
				p.WriteUnsignedInt( obj->Mode() );
				//Access Times (Long)
				p.WriteUnsignedInt( obj->mTime() );
				p.WriteUnsignedInt( obj->cTime() );
				
				if ( obj->IsFile() )
				{
					File *file = (File*)obj;
					//Size 
					p.WriteUnsignedInt( file->Size() );
					
					AddressList list = file->GetClique()->Members();
					
					p.WriteInt( list.size() );
					
					for(AddressList::const_iterator iter = list.begin(); iter != list.end(); iter++)
						p.WriteAddress( *iter );
				}
			}
			
			sock->Send( p );
			return true;
		}
		
		case FILE_UPDATE:
		{
			FSObject *newObj;
			char path[MAX_PATH];
			reader.ReadASCII( path, MAX_PATH );
			time_t mtime = reader.ReadUnsignedInt();
			off_t size = reader.ReadUnsignedInt();
			bool fwd = reader.ReadBool();
			
			if ( fwd && ThisIsAlpha() && !IsMember( sock->Addr() ) )
			{
				Packet p = reader.MakePacket();
				Broadcast( p );
			}
			
			newObj = FileSystem::GetObject( path );
			if ( newObj != NULL && newObj->IsFile() )
			{
				newObj->mTime( mtime );
				((File*)newObj)->Size( size );
			}
			
			return true;
		}
		
		case RM_DIR:
		case RM_FILE:
		{
			FSObject *obj;
			char path[MAX_PATH];
			
			reader.ReadASCII( path, MAX_PATH );
			
			obj = FileSystem::GetObject( path );
			
			if ( !obj )
				return false;
			
			if ( ThisIsAlpha() && !IsMember( sock->Addr() ) )
			{
				Packet p = reader.MakePacket();
				Broadcast( p );
			}
			
			FileSystem::RemoveObject( obj );
			
			return true;
		}
		
		case FORWARD_REQ:
		{
			NetAddress from = reader.ReadAddress();
			NetAddress to = reader.ReadAddress();
			
			if ( to == Socket::LocalAddr() )
			{
				if ( FindPeer( from ) == NULL )
				{
					Socket *sock = new Socket();
					if ( !sock->Connect( from, true ) )
						delete sock;
				}
			}
			else
			{
				Socket *fwd = FindPeer( to );
				if ( fwd != NULL )
				{
					Packet p = reader.MakePacket();
					fwd->Send( p );
				}
			}
			
			return true;
		}
		
		case RENAME:
		{
			char temp[MAX_PATH];
			
			reader.ReadASCII( temp, MAX_PATH );
			
			FSObject *obj = FileSystem::GetObject( temp );
			
			if ( !obj )
				return false;
			
			if ( ThisIsAlpha() && !IsMember( sock->Addr() ) )
			{
				Packet p = reader.MakePacket();
				Broadcast( p );
			}
			
			reader.ReadASCII( temp, MAX_PATH );
			
			obj->Move( temp );
			
			return true;
		}

		default: 
		{
			//cout << "Received " << (int)reader.Command() << endl;
			return false;
		}
	}
}







FileStorageClique::FileStorageClique( File *file ) : _File( file ), _DataID( 0 )
{
}

FileStorageClique::~ FileStorageClique()
{
}

void FileStorageClique::JoinClique( bool sync )
{
	Lock();
	for(AddressList::iterator iter = _Members.begin(); iter != _Members.end(); iter++)
	{
		if ( FindPeer( *iter ) == NULL )
		{
			Unlock();
			Socket *sock = new Socket();
			if ( !sock->Connect( *iter, !sync ) )
			{
				delete sock;
				
				/*Packet p( FORWARD_REQ );
				p.WriteAddress( Socket::LocalAddr() );
				p.WriteAddress( *iter );
				
				Alpha.SendOnce( p );*/
			}
			Lock();
		}
	}
	Unlock();
}

bool FileStorageClique::OnReceive( Socket *sock, PacketReader &reader )
{
	switch ( reader.Command() )
	{
		case OPEN_REQ:
		{
			char path[MAX_PATH];
			int flags;
			
			reader.ReadASCII( path, MAX_PATH );
			flags = reader.ReadInt();

			if ( FileSystem::GetObject( path ) != _File )
				return false;
			
			Packet p( OPEN_RESP, reader.RequestID() );
			
			if ( _File->IsWriting() && ( flags&O_WRONLY || flags&O_RDWR || flags&O_APPEND ) )
			{
				p.WriteInt( -EBUSY );
			}
			else
			{
				if ( _File->_Downloading )
					p.WriteInt( 0 );
				else
					p.WriteInt( _File->Version() );
				
				p.WriteAddress( Socket::LocalAddr() );
				
				AddMember( sock->Addr() );
			}
			
			sock->Send( p );
			
			return true;
		}
		
		case READ_REQ:
		{
			char path[MAX_PATH];
			int offset;
			
			reader.ReadASCII( path, MAX_PATH );
			
			if ( FileSystem::GetObject( path ) != _File )
				return false;
			
			offset = reader.ReadUnsignedInt();
						
			int size = BUFF_BLOCK_SIZE;
			
			if ( offset+size > _File->_Size )
				size = _File->_Size - offset;
			
			int end = offset+size;
			
			if ( size > 0 && end <= _File->_Size )
			{
				int timeout = time(NULL) + 10 + (end - _File->_Recvd)/(1<<24);
				while ( _File->_Downloading && _File->_Recvd < end && timeout > time(NULL) )
				{
					// wait for the network to get the data we need
					sched_yield();
					usleep( 5 );
				}
				
				if ( timeout <= time(NULL) )
					return false;
				
				_File->Lock();
				
				Packet p( DATA_BLOCK, reader.RequestID() );
				p.EnsureCapacity( size + PacketReader::PAYLOAD_BEGIN );
				p.WriteRaw( &_File->_Data[offset], size );
				
				_File->Unlock();
				
				sock->Send( p );
			}
			
			return true;
		}
		
		case DATA_BLOCK:
		{
			if ( reader.RequestID() != _DataID || !_File->_Downloading )
				return false;
			
			int len = reader.Length() - PacketReader::PAYLOAD_BEGIN;
			
			if ( _File->_Recvd+len > _File->_Size )
				len = _File->_Size - _File->_Recvd;
			
			if ( len > 0 )
			{
				_File->Lock();
				
				reader.ReadRaw( &_File->_Data[_File->_Recvd], len );
				_File->_Recvd += len;
				
				_File->Unlock();
			
				if ( _File->_Recvd < _File->_Size )
				{
					Packet p( READ_REQ );
					
					_DataID = p.RequestID();
					
					p.WriteASCII( _File->FullPath().c_str() );
					p.WriteUnsignedInt( _File->_Recvd );
					
					sock->Send( p );
				}
				else
				{
					_File->_Downloading = false;
					AddMember( Socket::LocalAddr() );
				}
			}
			
			return true;
		}
		
		case DRM_REQ:
		{
			char path[MAX_PATH];
			
			reader.ReadASCII( path, MAX_PATH );
			
			if ( FileSystem::GetObject( path ) != _File )
				return false;
			
			Packet p( DRM_RESP, reader.RequestID() );
			DRMManager->WriteDRM( _File, p );
			
			sock->Send( p );
			
			return true;
		}
		
		case RENAME:
		{
			char temp[MAX_PATH];
			
			reader.ReadASCII( temp, MAX_PATH );
			
			if ( FileSystem::GetObject( temp ) != _File )
				return false;
			
			reader.ReadASCII( temp, MAX_PATH );
			
			_File->Move( temp );
			
			return true;
		}
		
		case UPDATE_DRM:
		{
			char temp[MAX_PATH];
			
			reader.ReadASCII( temp, MAX_PATH );
			
			if ( FileSystem::GetObject( temp ) != _File )
				return false;
			
			DRMManager->ReadDRM( _File, reader );
			
			return true;
		}
		
		default:
		{
			return false;
		}
	}
}

void FileStorageClique::DownloadFrom( Socket *sock, int ver )
{	
	_File->Lock();
	
	_File->_Version = ver;
	
	_File->_Downloading = true;
	
	_File->_Recvd = 0;
	_File->_LocalSize = _File->_Size;
	_File->_Capacity = (_File->_Size/512 + 1)*512;
	if ( _File->_Data )
		delete[] _File->_Data;
	_File->_Data = new char[_File->_Capacity];
	
	Packet req( READ_REQ );
	req.WriteASCII( _File->FullPath().c_str() );
	req.WriteUnsignedInt( 0 );
	
	_DataID = req.RequestID();
	
	_File->Unlock();
	
	sock->Send( req );
}

void FileStorageClique::NoDownload()
{
	_File->Lock();
	
	if ( !_File->_Downloading )
		_File->_Recvd = _File->_LocalSize;
	_File->_Size = _File->_LocalSize;
	
	_File->Unlock();
}
