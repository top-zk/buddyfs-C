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

#ifndef __FILE_SYSTEM_H_
#define __FILE_SYSTEM_H_

#include <list>
#include <queue>
#include <fstream>
#include <iostream>
#include <string>

#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include "Buddy.h"
#include "Request.h"
#include "drm.h"

using namespace std;

#define LOCAL_CACHE_DURATION 5
#define BUFF_BLOCK_SIZE 4096

class FileSystem;
class FSObject;
class Folder; 
class File;

typedef list<FSObject*> FSList;
typedef FSList::iterator FSListIter;

class FileSystem
{
public:
	static void LoadLocal();
	static void SaveLocal();
	
	static void Slice();
	static bool RecurseExpire( FSObject * );
	
	static FSObject *GetObject( const char *path ); // path is assumed to be rooted at /, even if it doesnt begin with a /
	static FSObject *AddObject( const char *path, int type, bool brokenPaths = false ); // if brokenPaths is true, then there may be previously unknown folders in the path we're adding
	static void RemoveObject( FSObject *obj );
	
	static void CacheObject( FSObject *obj, int time = 5 );
	
	static void BuildList( list<string> &lst, FSObject *obj = (FSObject*)_Root, string path = "" );
	static void WriteFullList( Packet &p, FSObject *obj = (FSObject*)_Root, string path = "" );
	
	static Folder *GetRoot() { return _Root; }
	
private:
	static void RecurseSave( FSObject *obj, ofstream &data );
	
	static Folder *_Root;
	static int _LastSave;
};

class FSObject
{
public:
	explicit FSObject( const char *name, int type, FSObject *parent ) : _Parent( parent ), _Type( type ), _Name( NULL ), _Expire( 0 )
	{
		int len = strlen( name )+1;
		_Name = new char[len];
		memcpy( _Name, name, len );
		
		_Mode = 0777;
		
		_mTime = _cTime = time(NULL);
	}
	
	virtual ~FSObject()
	{
		delete[] _Name;
	}
	
	bool IsFolder() const { return _Type == DT_DIR; }
	bool IsFile() const { return _Type == DT_REG; }
	
	int Type() const { return _Type; }
	const char *Name() const { return _Name; }
	void Name(const char *name) 
	{
		int len = strlen( name )+1;
		delete[] _Name;
		_Name = new char[len];
		memcpy( _Name, name, len );
	}
	
	void Move( const char *to );
	
	string FullPath() const;
	
	FSObject *Parent() { return _Parent; }
	
	int CacheExpireTime() const { return _Expire; }
	
	virtual bool IsLocal() = 0;
	
	// File/Folder Common Attribute Function
	mode_t Mode() const { return _Mode; }
	void Mode( mode_t mode ) { _Mode = mode; }
	
	time_t mTime() const { return _mTime; }
	void mTime( time_t mtime ) { _mTime = mtime; }
	
	time_t cTime() const { return _cTime; }
	void cTime( time_t ctime ) { _cTime = ctime; }
	
private:
	friend class FileSystem;
	
	FSObject *_Parent;
	
	int _Type;
	char* _Name;
	int _Expire;
	
	mode_t _Mode;
	time_t _mTime, _cTime;
};

class Folder : public FSObject
{
public:
	explicit Folder( const char *name, FSObject *parent ) : FSObject( name, DT_DIR, parent )
	{
		FileSystem::CacheObject( this, LOCAL_CACHE_DURATION );
	}
	
	~Folder()
	{
	}
		
	FSList &GetList() { return _List; }
	
	virtual bool IsLocal() { return false; }
	
	
	
private:
	FSList _List;
};

class File : public FSObject, public Mutex
{
public:
	explicit File( const char *name, FSObject *parent );
	~File();
	
	int Read( void *data, unsigned int size, unsigned int offset = 0 );
	int Write( const void *data, unsigned int size, unsigned int offset = 0 );
	void Flush();

	void Open( int key, int flags );
	void Close( int key );
	
	bool IsReading() const { return IsOpen() && _Reads > 0; } 
	bool IsReading( int key );
	
	bool IsWriting() const { return IsOpen() && _Writing; }
	bool IsWriting( int key );
	
	bool IsOpen() const { return _Opens.size() > 0; }
	
	FileStorageClique *GetClique() { return _Clique; }
	
	bool IsLocal() { return _Version > 0; }
	//void SetLocal();
	
	off_t Size() const { return _Size; }
	void Size(off_t size) { _Size = size; }
	
	off_t LocalSize() const { return _LocalSize; }
	void LocalSize(off_t size) { _LocalSize = size; }
	
	int Version() const { return _Version; }
	void Version( int ver ) { _Version = ver; }
	
private:
	friend class FileSystem;
	friend class FileStorageClique;
	friend class DRM;
	
	FileStorageClique *_Clique;
	off_t _Size, _Capacity, _Recvd, _LocalSize;
	char *_Data;
	char *_WriteBuff;
	off_t _WBCap, _WBSize;
	
	bool _Writing;
	
	map<int,int> _Opens;
	int _Reads;
	
	int _Version;
	
	bool _Downloading;
};

#endif
