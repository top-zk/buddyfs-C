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

#include <dirent.h>

#include <list>
#include <iostream>
#include <fstream>
using namespace std;

#include "Buddy.h"
#include "FileSystem.h"
#include "drm.h"

Folder *FileSystem::_Root = new Folder( "/", NULL );
int FileSystem::_LastSave = 0;

void FileSystem::Slice()
{
	if ( _LastSave + 30 < time(NULL) )
		SaveLocal();
	
	if ( !Alpha.ThisIsAlpha() )
	{
		_Root->_Expire = 1;
	
		RecurseExpire( _Root );
	}
}

bool FileSystem::RecurseExpire( FSObject *obj )
{
	bool expired = !obj->IsLocal() && obj->CacheExpireTime() > 0 && obj->CacheExpireTime() < time(NULL);
	
	if ( expired && obj->IsFolder() )
	{
		expired = true;
		FSList list = ((Folder*)obj)->GetList();
		for(FSList::iterator iter=list.begin(); iter != list.end(); iter++)
			expired &= RecurseExpire( *iter );
	}
	
	if ( expired )
	{
		FSObject *p = obj->Parent();
		
		if ( p != NULL && p->IsFolder() )
		{
			FSList &list = ((Folder*)p)->GetList();
			for(FSList::iterator iter=list.begin(); iter != list.end(); iter++)
			{
				if ( *iter == obj )
				{
					list.erase( iter );
					break;
				}
			}
		}
		
		if ( obj != _Root )
			delete obj;
	}
	
	return expired;
}


void FileSystem::LoadLocal()
{
	char fileName[MAX_PATH];
	sprintf( fileName, "%s/local_data", BuddyDir );
	ifstream data( fileName, ios::in | ios::binary );
	if ( !data )
		return;
	
	data.peek();
	
	while ( data.good() )
	{
		char temp[MAX_PATH];
		int type;
		int len;
		char *buff;
		data.seekg( 1, ios::cur );
		data.read( (char*)&len, sizeof(int) );
		len = ntohl(len);
		data.seekg( -5, ios::cur );
		
		buff = new char[len];
		data.read( buff, len );
		
		data.peek();
		
		PacketReader reader( buff, len ); // attaches to buff and will delete it when deconstructed
		
		type = reader.ReadByte();
		reader.ReadASCII( temp, MAX_PATH );
		
		FSObject *obj = AddObject( temp, type );
		
		if ( !obj )
			continue;
		
		// read generic FSObject stuff into obj here
		//Mode (Unsigned Integer)
		obj->Mode( reader.ReadUnsignedInt() );
		//Access Times (Long)
		obj->mTime( (time_t)reader.ReadUnsignedInt() );
		obj->cTime( (time_t)reader.ReadUnsignedInt() );

		switch ( type )
		{
			case DT_DIR:
			{
				//Folder *fld = (Folder*)obj;
			
				// read Folder specific stuff into fld here
				
				break;	
			}
			case DT_REG:
			{
				File *file = (File*)obj;
				
				file->Lock();
				
				// read File specific stuff into file here
				bool local = reader.ReadBool();
				
				if ( local )
				{
					file->_Version = reader.ReadInt();
					
					file->_Capacity = file->_LocalSize = file->_Size = reader.ReadUnsignedInt();
					
					if ( file->_LocalSize % BUFF_BLOCK_SIZE != 0 )
						file->_Capacity = (file->_LocalSize/BUFF_BLOCK_SIZE + 1)*BUFF_BLOCK_SIZE;
					
					if ( file->_Capacity <= 0 )
						file->_Capacity = BUFF_BLOCK_SIZE;
					
					file->_Data = new char[file->_Capacity];
					
					DRMManager->ReadDRM( file, reader );
					
					if ( file->_LocalSize > 0 )
						DRMManager->Decrypt( file, reader );
					
					file->GetClique()->AddMember( Socket::LocalAddr() );
				}
				
				file->Unlock();
				
				break;
			}
		}
	}
	
	data.close();
	
	_LastSave = time(NULL);
}

void FileSystem::SaveLocal()
{
	static bool errored = false;
	
	char fileName[MAX_PATH];
	sprintf( fileName, "%s/local_data", BuddyDir );
	ofstream data( fileName, ios::out|ios::binary );
	
	if ( !data )
	{
		if ( !errored )
		{
			cerr << "PANIC! Unable to save local data! Everything is going black! Aaarrrgggghhhh!!" << endl;
			cerr << "...Move towards the light, Buddy...." << endl;
			errored = true;
		}
		return;
	}
	
	RecurseSave( _Root, data );
	
	data.close();
	
	_LastSave = time(NULL);
}

void FileSystem::RecurseSave( FSObject *obj, ofstream &data )
{
	if ( strcmp( obj->Name(), "/" ) )
	{
		Packet p( 0, 0, 128 ); // default buffer capacity
		
		p.WriteByte( obj->Type() );
		
		p.WriteASCII( obj->FullPath().c_str() );
		
		// write generic FSObject stuff here
		//Mode (Unsigned Integer)
		p.WriteUnsignedInt( obj->Mode() );
		//Access Times (Long)
		p.WriteUnsignedInt( obj->mTime() );
		p.WriteUnsignedInt( obj->cTime() );
		
		switch ( obj->Type() )
		{
			case DT_DIR:
			{
				//Folder *fld = (Folder*)obj;
					
				// write Folder specific stuff from fld here
						
				break;	
			}
			case DT_REG:
			{
				File *file = (File*)obj;
						
				file->Lock();
				// write File specific stuff from file here
				
				if ( file->IsLocal() && !file->_Downloading )
				{
					p.EnsureCapacity( file->_LocalSize + 256 );
					
					p.WriteBool( true );
					p.WriteInt( file->_Version );
					p.WriteUnsignedInt( file->_LocalSize );
					
					DRMManager->WriteDRM( file, p );
					
					DRMManager->Encrypt( file, p );
				}
				else
				{
					p.WriteBool( false );
				}
				
				file->Unlock();
				
				break;
			}
		}
		
		data.write( p.Buffer(), p.Length() );
	}
	
	if ( obj->IsFolder() )
	{
		Folder *fld = (Folder*)obj;
		
		FSList list = fld->GetList();
		for ( FSList::iterator iter = list.begin(); iter != list.end(); iter++ )
			RecurseSave( *iter, data );
	}
}

void FileSystem::RemoveObject( FSObject *obj )
{
	if ( obj == NULL || obj == _Root || obj->Parent() == NULL )
		return;
	
	FSList *list;
	
	if ( obj->IsFolder() )
	{
		list = &((Folder*)obj)->GetList();
		
		while ( list->size() > 0 )
			FileSystem::RemoveObject( *list->begin() );
	}
	else
	{
		((File*)obj)->Lock(); // need to lock the file to remove it
	}
	
	list = &((Folder*)obj->Parent())->GetList();
	for(FSList::iterator iter = list->begin(); iter != list->end(); iter++)
	{
		if ( *iter == obj )
		{
			list->erase( iter );
			break;
		}
	}
	
	delete obj; // it is EXPECTED if its a file it is locked before delete is called. Unlock called by the dtor
}

FSObject *FileSystem::AddObject( const char *path, int type, bool brokenPaths )
{
	char temp[MAX_PATH];
	const char *ptr = path;
	Folder *cur = _Root, *last;
	
	while ( *ptr == '/' ) // strip leading /s
		ptr++;

	while ( *ptr && cur )
	{
		char *dest = temp;
		while ( *ptr && *ptr != '/' )
			*dest++ = *ptr++;
		*dest = 0;
		
		while ( *ptr == '/' ) // skip multiple /s
			ptr++;
		
		last = cur;
		cur = NULL;
		
		FSList &list = last->GetList();
		
		for( FSList::iterator iter = list.begin(); iter != list.end() && cur == NULL; iter++ )
		{
			if ( !strcmp( (*iter)->Name(), temp ) )
			{
				if ( (*iter)->IsFolder() )
					cur = (Folder*)*iter;
				else
					return NULL;
			}
		}
		
		if ( brokenPaths && *ptr && !cur )
		{
			Folder *brokenPath = new Folder( temp, last );
			last->GetList().push_back( brokenPath );
			cur = brokenPath;
		}
	}
	
	if ( cur && cur != _Root )
		return NULL; // tried to add a folder that already exists
	
	FSObject *newObj;
	if ( type == DT_DIR )
		newObj = new Folder( temp, last );
	else
		newObj = new File( temp, last );
	
	last->GetList().push_back( newObj );
	return newObj;
}

FSObject *FileSystem::GetObject( const char *path )
{
	//Is the FSObj in the Cache?
	char temp[MAX_PATH];
	const char *ptr = path;
	FSObject *cur = _Root;
	
	while ( *ptr == '/' )
		ptr++;
	
	while ( *ptr && cur )
	{
		if ( !cur->IsFolder() )
		{
			//cout << "Get obj not folder " << ptr << "...." << endl;
			cur = NULL;
			break;
		}
		
		char *dest = temp;
		while ( *ptr && *ptr != '/' )
			*dest++ = *ptr++;
		*dest = 0;
		
		while ( *ptr == '/' ) // skip extra /s
			ptr++;
		
		FSList &list = ((Folder*)cur)->GetList();
		
		cur = NULL;
		for( FSList::iterator iter = list.begin(); iter != list.end() && cur == NULL; iter++ )
		{
			if ( !strcmp( (*iter)->Name(), temp ) )
				cur = *iter;
		}
	}
	
	//If the FSObj is not in the cache (or I need to request a full file record) and I am not the AlphaClique
	if ( cur == NULL && !Alpha.ThisIsAlpha() )
	{
		Packet req( FS_REQ );
		req.WriteASCII( path );
		
		NetworkRequest::Register( FS_RESP, req.RequestID() );
			
		if ( !Alpha.SendOnce( req ) )
			return NULL;
		
		if ( !NetworkRequest::WaitForResponse( req.RequestID() ) )
			return NULL;
		
		PacketReader reader = NetworkRequest::GetResponse( req.RequestID() );
		
		if ( !reader.IsValid() || reader.Command() != FS_RESP )
			return NULL;
		
		//AlphaClique has file info	
		int val = reader.ReadShort();
		if ( val == 1 )
		{ 
			reader.ReadASCII( temp, MAX_PATH );
			char type = reader.ReadByte();
			
			cur = AddObject( temp, type );
			
			if ( !cur )
				cur = GetObject( temp );
			
			if ( !cur )
				return NULL;
			
			//Mode (Unsigned Integer)
			cur->Mode( reader.ReadUnsignedInt() );
			//Access Times (Long)
			cur->mTime( reader.ReadUnsignedInt() );
			cur->cTime( reader.ReadUnsignedInt() );

			if ( cur->IsFile() )
			{
				File *fcur = (File*)cur;
				
				fcur->Size( (size_t)reader.ReadUnsignedInt() );
				
				int count = reader.ReadInt();
				
				for(int i=0;i<count;i++)
					fcur->GetClique()->AddMember( reader.ReadAddress() );
			}
		}
		else 
		{
			errno = abs(val);
			return NULL;
		}
	}
	
	return cur;
}

void FileSystem::BuildList( list<string> &lst, FSObject *obj, string currentPath )
{
	if ( obj == NULL )
		return;
	
	if ( obj != _Root )
		currentPath += "/" + string( obj->Name() );
	
	if ( obj->IsFile() )
	{
		if ( ((File*)obj)->IsLocal() )
			lst.push_back( currentPath );
		return;
	}
	
	Folder *fld = (Folder*)obj;
	
	for(FSListIter iter = fld->GetList().begin(); iter != fld->GetList().end(); iter++)
		BuildList( lst, *iter, currentPath );
}

void FileSystem::WriteFullList( Packet &p, FSObject *obj, string currentPath )
{
	if ( obj == NULL )
		return;
	
	if ( obj != _Root )
	{
		currentPath += "/";
		currentPath += obj->Name();
	
		p.WriteASCII( currentPath.c_str() );
		p.WriteByte( obj->Type() );
		p.WriteUnsignedInt( obj->Mode() );
		p.WriteUnsignedInt( obj->mTime() );
		p.WriteUnsignedInt( obj->cTime() );
	}
	if ( obj->IsFile() )
	{
		File *file = (File*)obj;

		p.WriteUnsignedInt( file->Size() );
						
		AddressList list = file->GetClique()->Members();
						
		p.WriteInt( list.size() );
						
		for(AddressList::const_iterator iter = list.begin(); iter != list.end(); iter++)
			p.WriteAddress( *iter );
	}
	else
	{
		Folder *fld = (Folder*)obj;
		for(FSListIter iter = fld->GetList().begin(); iter != fld->GetList().end(); iter++)
			WriteFullList( p, *iter, currentPath );
	}
}

void FileSystem::CacheObject( FSObject *obj, int exipre )
{
	obj->_Expire = time(NULL) + exipre;
}



void FSObject::Move( const char *to )
{
	if ( _Parent == NULL )
		return;
	
	FSList *list = &((Folder*)_Parent)->GetList();
	for ( FSList::iterator iter = list->begin(); iter != list->end(); iter++ )
	{
		if ( *iter == this )
		{
			list->erase( iter );
			break;
		}
	}
	
	// the following was copied almost exactly from AddObject
	char temp[MAX_PATH];
	const char *ptr = to;
	Folder *cur = FileSystem::GetRoot(), *last;
	
	while ( *ptr == '/' ) // strip leading /s
		ptr++;

	while ( *ptr && cur )
	{
		char *dest = temp;
		while ( *ptr && *ptr != '/' )
			*dest++ = *ptr++;
		*dest = 0;
		
		while ( *ptr == '/' ) // skip multiple /s
			ptr++;
		
		last = cur;
		cur = NULL;
		
		FSList &list = last->GetList();
		
		for( FSList::iterator iter = list.begin(); iter != list.end() && cur == NULL; iter++ )
		{
			if ( !strcmp( (*iter)->Name(), temp ) )
			{
				if ( (*iter)->IsFolder() )
					cur = (Folder*)*iter;
				else
					return;
			}
		}
		
		if ( *ptr && !cur )
		{
			Folder *brokenPath = new Folder( temp, last );
			last->GetList().push_back( brokenPath );
			cur = brokenPath;
		}
	}
	
	Name( temp );
	
	if ( cur && cur != FileSystem::GetRoot() )
		return; // tried to add a folder that already exists
	
	_Parent = last;
	last->GetList().push_back( this );
}

string FSObject::FullPath() const
{
	FSObject *o = _Parent;
	string path = Name();
	
	while ( o )
	{
		path = o->Name() + string("/") + path;
		o = o->Parent();
	}
	
	return path.c_str()+1; // skip the extra root "/"
}



File::File( const char *name, FSObject *parent ) : FSObject( name, DT_REG, parent ), 
	_Clique( new FileStorageClique( this ) ), _Size( 0 ), _Capacity( 0 ), _Recvd( 0 ), _LocalSize( 0 ), _Data( NULL ),
	_WriteBuff( NULL ), _WBCap( 0 ), _WBSize( 0 ), _Writing( false ), _Reads( 0 ), _Version( 0 ), _Downloading( false )
{
}
	
File::~File()
{
	// !! must be locked when deleted !!
	
	delete[] _Data;
	delete[] _WriteBuff;
	delete _Clique;
		
	Unlock();
}
	
void File::Open( int key, int flags )
{
	Lock();
	
	flags &= O_ACCMODE;
	
	if ( flags == O_WRONLY || flags == O_RDWR || flags == O_APPEND )
	{
		_Writing = true;
		
		if ( _WriteBuff )
			delete[] _WriteBuff;
		
		if ( _Capacity && _LocalSize )
		{
			_WriteBuff = new char[_Capacity];
			memcpy( _WriteBuff, _Data, _LocalSize );
			
			_WBCap = _Capacity;
			_WBSize = _LocalSize;
		}
		else
		{
			_WriteBuff = NULL;
			_WBCap = _WBSize = 0;
		}
	}
	
	if ( flags == O_RDONLY || flags == O_RDWR )
		_Reads++;
	
	_Opens.insert( pair<int,int>( key, flags ) );
	
	Unlock();
}

void File::Close( int key )
{
	Lock();
	
	map<int,int>::iterator iter = _Opens.find( key );
	if ( iter == _Opens.end() )
		return;
	
	int flags = iter->second;
	
	if ( flags == O_WRONLY || flags == O_RDWR || flags == O_APPEND )
	{
		_Writing = false;
		
		delete[] _WriteBuff;
		_WriteBuff = NULL;
		_WBCap = _WBSize = 0;
	}
	
	if ( flags == O_RDONLY || flags == O_RDWR )
		_Reads--;
	
	_Opens.erase( iter );
	
	Unlock();
}

bool File::IsReading( int key )
{
	if ( !IsReading() )
		return false;
	
	Lock();
	map<int,int>::iterator iter = _Opens.find( key );
	Unlock();
	if ( iter == _Opens.end() )
		return false;

	int flags = iter->second;
	return ( flags == O_RDONLY || flags == O_RDWR );
}

bool File::IsWriting( int key )
{
	if ( !IsWriting() )
		return false;
	
	Lock();
	map<int,int>::iterator iter = _Opens.find( key );
	Unlock();
	if ( iter == _Opens.end() )
		return false;

	int flags = iter->second;
	return ( flags == O_WRONLY || flags == O_RDWR || flags == O_APPEND );
}

int File::Read( void *data, unsigned int size, unsigned int offset )
{
	if ( _Data == NULL || _LocalSize <= 0 )
		return 0;
	
	if ( offset > _LocalSize )
		return 0;
	
	if ( offset + size > _LocalSize )
		size = _LocalSize - offset;
	
	int end = offset+size;
	
	int timeout = time(NULL) + 10 + (end-_Recvd)/(1<<24);
	while ( _Downloading && _Recvd < end && timeout > time(NULL) )
	{
		// wait for the network to get the data we need
		sched_yield();
		usleep( 5 );
	}
	
	if ( timeout <= time(NULL) )
		return -ETIMEDOUT;
	
	Lock();
	
	if ( _Recvd < end )
	{
		if ( _Recvd < offset )
			size = 0;
		else
			size = _Recvd - offset;
	}
	
	if ( size > 0 )
		memcpy( data, &_Data[offset], size );
	
	Unlock();
	
	return size;
}

int File::Write( const void *data, unsigned int size, unsigned int offset )
{
	Lock();
	
	size_t end = offset + size;
	
	cout << "Write s:" << size << " at o:" << offset << " -- LocalSize = " << _LocalSize << endl;
	
	if ( offset >= _LocalSize ) // trying to write past the end means appending
	{
		cout << "Append! " << DRMManager->CanAppend( this ) << endl;
		if ( !DRMManager->CanAppend( this ) )
			return -EACCES; 
	}
	else
	{
		cout << "Write! " << DRMManager->CanWrite( this ) << endl;
		if ( !DRMManager->CanWrite( this ) )
			return -EACCES; 
	}
	
	if ( end > _WBCap )
	{
		_WBCap = (end/BUFF_BLOCK_SIZE + 1) * BUFF_BLOCK_SIZE;
		
		char *old = _WriteBuff;
		
		_WriteBuff = new char[_WBCap];
		if ( old && _WBSize )
		{
			memcpy( _WriteBuff, old, _WBSize );
			delete[] old;
		}
	}
	
	memcpy( &_WriteBuff[offset], data, size );
	
	if ( end > _WBSize )
		_WBSize = end;
	
	Unlock();
	
	return size;
}

void File::Flush()
{
	if ( !DRMManager->CanAppend( this ) && !DRMManager->CanWrite( this ) )
		return; 

	Lock();
		
	if ( _Data )
		delete[] _Data;
	
	if ( _WriteBuff )
	{
		_Data = new char[_WBCap];	
		memcpy( _Data, _WriteBuff, _WBSize );
		
		_Capacity = _WBCap;
		_Recvd = _LocalSize = _Size = _WBSize;
	}
	else
	{
		_Data = new char[BUFF_BLOCK_SIZE];
		
		_Capacity = BUFF_BLOCK_SIZE;
		_Recvd = _Size = _LocalSize = 0;
	}
	
	_Downloading = false;
	
	Unlock();
}
