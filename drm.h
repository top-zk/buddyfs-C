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

#ifndef __DRM_H_
#define __DRM_H_

#include "Buddy.h"
#include "Socket.h"
#include "FileSystem.h"
#include <vector>
#include <string>
#include <iostream>
#include <arpa/inet.h>
#include <openssl/blowfish.h>
#include <openssl/md5.h>

using namespace std;

/**
 * Permissions
 *  4 bits, remove, write, append, read.
 *  0            0      0       0     0
 *  1            0      0       0     1
 *  2            0      0       1     0
 *  4            0      1       0     0
 *  8            1      0       0     0
 *
 *  etc...
 **/

class MD5Hash
{
public:
	explicit MD5Hash()
	{
		memset(_Hash, 0, 16);
	}

	MD5Hash(const MD5Hash &copy)
	{
		memcpy(_Hash, copy._Hash, 16);
	}

	~MD5Hash()
	{
	}

	const MD5Hash &operator=(const MD5Hash &copy)
	{
		if (this != &copy)
			memcpy(_Hash, copy._Hash, 16);
		return *this;
	}

	bool operator==(const MD5Hash &rhs) const
	{
		return !memcmp(_Hash, rhs._Hash, 16);
	}

	void Read(PacketReader &reader)
	{
		reader.ReadRaw(_Hash, 16);
	}

	void Write(Packet &p) const
	{
		p.WriteRaw(_Hash, 16);
	}

	void HashFile(const char *name);

	string ToString();
	void FromString(const string &str);

private:
	unsigned char _Hash[16];
};

class DRM
{
public:
	DRM();

	// Network DRM Functions
	void ReadDRM(FSObject *fsobj, PacketReader &reader);
	void WriteDRM(FSObject *fsobj, Packet &p);

	bool isDRM(FSObject *fsobj);
	int GetPerms(FSObject *fsobj);

	bool CanAppRead(const MD5Hash &appHash, File *file);
	bool CanAppWrite(const MD5Hash &appHash, File *file);

	void AddUser(string name, string password);
	void AddFile(FSObject *file);

	bool CanRead(FSObject *fsobj);
	bool CanWrite(FSObject *fsobj);
	bool CanAppend(FSObject *fsobj);
	bool CanRemove(FSObject *fsobj);
	bool IsSiteAllowed(FSObject *fsobj, const NetAddress &addr);
	bool IsOwner(FSObject *fsobj);

	// Encryption Functions
	void Encrypt(File *file, Packet &p);
	void Decrypt(File *file, PacketReader &reader);

	int ListXAttr(File *file, char *buff, int size);
	int GetXAttr(File *file, const char *name, char *value, size_t size);
	int SetXAttr(File *file, const char *name, const char *value, size_t size, int flags);
	int RemoveXAttr(File *file, const char *name);

private:
	struct User
	{
		string name, passwd;
	};

	struct Group
	{
		string group_name;
		vector<User *> members;
	};

	typedef map<Group *, int> GroupMap;

	struct Rights
	{
		// Individual User Rights
		int ownerperms;
		vector<User *> owners;
		GroupMap groups;
		int others;

		// Rights for Replication
		int num_replicas;
		// Deny all and allow some (true) or deny some and allow all (false)
		bool order_deny_allow;
		vector<NetAddress> allowed_sites;
		vector<NetAddress> denied_sites;

		bool allow_all_apps;
		vector<MD5Hash> allowed_applications;
	};

	typedef map<FSObject *, Rights> RightsMap;

	// All known users/groups in the system
	vector<User *> users;
	vector<Group *> groups;
	// All the managed files
	RightsMap _ManagedFiles;
	// Rights for unmanaged files
	Rights *_Default;

	User *_Curr;
	vector<Group *> _CurrGroups;

	int AddPerms(int perm1, int perm2);
	vector<string> split(string str, char c);
	User *GetUser(string username);
	Group *GetGroup(string gname);
};

extern DRM *DRMManager;

#endif
