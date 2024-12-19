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
#include <string>
#include <iostream>
#include <fstream>
#include <arpa/inet.h>
#include <openssl/evp.h>
#include <openssl/blowfish.h>
#include <openssl/md5.h>
#include <fstream>
#include <sys/types.h>
#include <attr/xattr.h>

#include "Buddy.h"
#include "FileSystem.h"
#include "drm.h"

using namespace std;

void HexDump( ostream &out, const char *data, int len );

void MD5Hash::HashFile( const char *name )
{
	ifstream in( name, ios::in|ios::binary );	
	if ( !in )
		return;
	
	char data[1024];
	MD5_CTX ctx;
	int len;
	
	MD5_Init( &ctx );
	in.seekg( 0, ios::end );
	len = in.tellg();
	in.seekg( 0, ios::beg );
	
	int total = 0;
	do
	{
		int get = len - total;
		if ( get > 1024 )
			get = 1024;
			
		in.read( data, get );
			
		if ( get > 0 )
			MD5_Update( &ctx, data, get );
		else
			break;
			
		total += get;
	} while ( total < len );
		
	in.close();
		
	MD5_Final( _Hash, &ctx );
}

string MD5Hash::ToString()
{
	char done[128];
	char temp[4];
		
	*done = 0;
		
	for(int i=0;i<16;i++)
	{
		snprintf( temp, 4, "%02x", (int)_Hash[i] );
		strcat( done, temp );
	}
		
	return done;
}

void MD5Hash::FromString( const string &str )
{
	char temp[33];
	memset( temp, '0', 32 );
	
	int len = str.length();
	if ( len > 32 )
		len = 32;
	
	memcpy( &temp[32-len], str.c_str(), len );
	temp[32] = 0;
		
	for(int i=0;i<16;i++)
	{
		unsigned int ch = 0;
		
		ch = tolower( temp[i*2+0] );
		if ( ch < '0' || ch > '9' )
			ch = ch - 'a' + 10;
		else
			ch = ch - '0';
		_Hash[i] = (unsigned char)(ch << 4);
		
		ch = tolower( temp[i*2+1] );
		if ( ch < '0' || ch > '9' )
			ch = ch - 'a' + 10;
		else
			ch = ch - '0';
		_Hash[i] |= (unsigned char)ch;
	}
}

DRM::DRM()
{
	//Initialize Default DRM Settings Here from user home Directory
	char temp[MAX_PATH];
	sprintf( temp, "%s/drm.conf", BuddyDir );
	ifstream file( temp );
	if ( !file )
		cout << "Unable to open " << temp << ": " << strerror(errno) << endl;
	//Default Values
	_Curr = NULL;
	_Default = new Rights;
	
	_Default->ownerperms = 15;
	_Default->others = 15;
	_Default->order_deny_allow = false;
	_Default->num_replicas = 0x7FFFFFFF;
	_Default->allow_all_apps = true;
	
	string line;
	while ( file.good() )
	{
		getline(file, line);
		
		//Ignore comments
		if ( line.length() <= 0 || line[0] == '#' )
			continue;

		unsigned int i = 0;
		string lhs = "";
		//Check for left side arg
		while (i < line.length() && line[i] != '\n' && line[i] != '=')
		{
			lhs += line[i];
			i++;
		}
		
		string rhs = "";
		if (line[i] == '=')
		{
			i++;
			while (i < line.length() && line[i] != '\n')
			{
				rhs += line[i];
				i++;
			}
		}
		
		if (lhs == "[users]")
		{
			getline(file, rhs);
			while (rhs != "[end]" && file.good())
			{
				vector<string> userpass = split(rhs, ',');
				
				AddUser(userpass[0], userpass[1]);
				
				cout << "Added User " << userpass[0] << endl;
				
				getline(file, rhs);
			}
			
		}
		else if (lhs == "[groups]")
		{
			getline(file, rhs);
			while (rhs != "[end]" && file.good())
			{
				vector<string> gname = split(rhs, '=');
				vector<string> members = split(gname[1], ',');
				
				Group *newgrp = new Group;
				
				newgrp->group_name = gname[0];
				
				for (i = 0; i < members.size(); i++)
				{
					if (GetUser(members[i]) != NULL)
						newgrp->members.push_back(GetUser(members[i]));
					
				}
				
				groups.push_back(newgrp);
				
				cout << "Added Group " << gname[0] << endl;
				
				getline(file, rhs);
			}
		}
		else if (lhs == "ownerperms")
		{
			if (_Default == NULL) _Default = new Rights;
			
			_Default->ownerperms = atoi(rhs.c_str());
			
			cout << "Set ownerperms to " << rhs << endl;
		}
		else if (lhs == "[owners]" || lhs == "[defaultgroups]")
		{
			if (_Default == NULL) _Default = new Rights;
			//NOTE: this only adds users/groups already in the system.
			
			vector<string> list;
			getline(file, rhs);			
			while (rhs != "[end]")
			{
				list.push_back(rhs);
				getline(file, rhs);
			}
			
			for (i = 0; i < list.size(); i++)
			{
				if (lhs == "[owners]")
				{
					_Default->owners.push_back(GetUser(list[i]));
					
					cout << "Set owner " << list[i] << endl;
				}
				else if (lhs == "[defaultgroups]")
				{
					vector<string> grp = split(list[i], '=');
					
					Group *g = GetGroup(grp[0]);
					if (g != NULL)
						_Default->groups[g] = atoi(grp[1].c_str());
						
					cout << "Set group " << g->group_name << endl;
				}
			}
		}
		else if (lhs == "[others]")
		{
			if (_Default == NULL) _Default = new Rights;
			
			_Default->others = atoi(rhs.c_str());
			
			cout << "Set others perms to " << rhs << endl;
		}
		else if (lhs == "order")
		{
			if (_Default == NULL) _Default = new Rights;
			
			if (rhs == "deny") _Default->order_deny_allow = true;
			else _Default->order_deny_allow = false;
			
			cout << "Set order bit to " << rhs << endl;
		}
		else if (lhs == "[allowed]" || lhs == "[denied]")
		{
			if (_Default == NULL) _Default = new Rights;
			
			getline(file, rhs);
			vector<NetAddress> addresses;
			while (rhs != "[end]" && file.good())
			{
				addresses.push_back(NetAddress(inet_addr(rhs.c_str()), 0));
				
				cout << "Picked up address " << rhs << endl;
				
				getline(file, rhs);
			}
			
			if (lhs == "[allowed]")
			{
				_Default->allowed_sites = addresses;
				cout << "Set to allowed" << endl;
			}
			else
			{
				_Default->denied_sites = addresses;
				cout << "Set to denied" << endl;
			}
		}
		else if (lhs == "allowapps")
		{
			if (_Default == NULL) _Default = new Rights;
			
			if (rhs == "no") _Default->allow_all_apps = false;
			else _Default->allow_all_apps = true;
			
			cout << "Set allowapps bit to " << rhs << endl;
		}
		else if (lhs == "[apps]")
		{
			if (_Default == NULL) _Default = new Rights;
			
			getline(file, rhs);
			vector<MD5Hash> apps;
			while (rhs != "[end]" && file.good())
			{
				MD5Hash hash;
				hash.FromString( rhs );
				
				apps.push_back( hash );
				
				cout << "Picked up app " << hash.ToString() << endl;
				
				getline(file, rhs);
			}
			
			cout << "Setting apps" << endl;
			_Default->allowed_applications = apps;
		}
	}

	while ( _Curr == NULL )
	{
		cout << endl << "Enter BuddyFS username: ";
		cin.getline(temp,MAX_PATH);
		
		_Curr = GetUser( temp );
		
		if ( _Curr == NULL )
		{
			cout << "User not found, try again." << endl;
		}
		else
		{
			cout << "Enter password: ";
			cin.getline(temp,MAX_PATH);
			
			if ( temp != _Curr->passwd )
			{
				cout << "Password incorrect." << endl;
				_Curr = NULL;
				sleep(1);
			}
		}
	}
	
	for (unsigned int i = 0; i < groups.size(); i++)
	{
		Group* curr = groups[i];
		for (unsigned int j = 0; j < curr->members.size(); j++)
		{
			if (curr->members[j] == _Curr)
			{
				_CurrGroups.push_back(curr);
				break;
			}
		}
	}
}

//Network DRM Functions
void DRM::ReadDRM(FSObject *fsobj, PacketReader &reader)
{
	if ( reader.ReadBool() )
	{
		Rights r;
		int count;
		char temp[MAX_PATH];
		
		r.ownerperms = reader.ReadInt();
		
		count = reader.ReadInt();
		for(int i=0;i<count;i++)
		{
			reader.ReadASCII( temp, MAX_PATH );
			User *u = GetUser( temp );
			if ( u )
				r.owners.push_back( u );
		}
		
		count = reader.ReadInt();
		for(int i=0;i<count;i++)
		{
			reader.ReadASCII( temp, MAX_PATH );
			int perms = reader.ReadInt();
			Group *g = GetGroup( temp );
			if ( g )
				r.groups.insert( GroupMap::value_type( g, perms ) );
		}
		
		r.others = reader.ReadInt();
		r.num_replicas = reader.ReadInt();
		r.order_deny_allow = reader.ReadBool();
		
		count = reader.ReadInt();
		for(int i=0;i<count;i++)
			r.allowed_sites.push_back( reader.ReadAddress() );
		
		count = reader.ReadInt();
		for(int i=0;i<count;i++)
			r.denied_sites.push_back( reader.ReadAddress() );
		
		r.allow_all_apps = reader.ReadBool();
		count = reader.ReadInt();
		for(int i=0;i<count;i++)
		{
			MD5Hash tmp;
			tmp.Read( reader );
			r.allowed_applications.push_back( tmp );
		}
		
		RightsMap::iterator iter = _ManagedFiles.find( fsobj );
		if ( iter != _ManagedFiles.end() )
			_ManagedFiles.erase( iter );
		_ManagedFiles.insert( RightsMap::value_type( fsobj, r ) );
	}
}

void DRM::WriteDRM(FSObject *fsobj, Packet &p)
{
	if ( _ManagedFiles.count(fsobj) > 0 )
	{
		Rights &r = _ManagedFiles[fsobj];
		
		p.WriteBool( true );
		
		p.WriteInt( r.ownerperms );
		
		p.WriteInt( r.owners.size() );
		for(unsigned int i=0;i<r.owners.size();i++)
			p.WriteASCII( r.owners[i]->name.c_str() );
		
		p.WriteInt( r.groups.size() );
		for(GroupMap::iterator i=r.groups.begin();i!=r.groups.end();i++)
		{
			p.WriteASCII( i->first->group_name.c_str() );
			p.WriteInt( i->second );
		}
		
		p.WriteInt( r.others );
		
		p.WriteInt( r.num_replicas );
		
		p.WriteBool( r.order_deny_allow );
		
		p.WriteInt( r.allowed_sites.size() );
		for(unsigned int i=0;i<r.allowed_sites.size();i++)
			p.WriteAddress( r.allowed_sites[i] );
		
		p.WriteInt( r.denied_sites.size() );
		for(unsigned int i=0;i<r.denied_sites.size();i++)
			p.WriteAddress( r.denied_sites[i] );
		
		p.WriteBool( r.allow_all_apps );
		p.WriteInt( r.allowed_applications.size() );
		for(unsigned int i=0;i<r.allowed_applications.size();i++)
			r.allowed_applications[i].Write( p );
	}
	else
	{
		p.WriteBool( false );
	}
}

bool DRM::isDRM(FSObject *fsobj)
{
	if (_ManagedFiles.count(fsobj) > 0)
	{
		return true;
	}
	
	return false;
}

void listbuffadd( char *buff, const char *toAdd, int &pos, int size )
{
	if ( pos >= size )
		return;
	
	int len = strlen( toAdd ) + 1;
	
	if ( pos+len > size )
		return;
	
	memcpy( &buff[pos], toAdd, len );
	pos += len;
}

int DRM::ListXAttr( File *file, char *buff, int size )
{
	Rights *r = _Default;
	if ( _ManagedFiles.count(file) > 0 )
		r = &_ManagedFiles[file];
	
	if ( !r )
		return -EFAULT;
	
	char temp[MAX_PATH];
	
	int pos = 0;
	listbuffadd( buff, "user.owner-perm", pos, size );
	
	listbuffadd( buff, "user.order_deny_allow", pos, size );
	
	for(unsigned int i=0;i<r->owners.size();i++)
	{
		sprintf( temp, "user.owner%d", i+1 );
		listbuffadd( buff, temp, pos, size );
	}
	
	for(unsigned int i=0;i<r->groups.size();i++)
	{
		sprintf( temp, "user.group%d", i+1 );
		listbuffadd( buff, temp, pos, size );
		
		sprintf( temp, "user.group-perm%d", i+1 );
		listbuffadd( buff, temp, pos, size );
	}
	
	listbuffadd( buff, "user.anon-perm", pos, size );
	
	//listbuffadd( buff, "user.num-replicas", pos, size );
	
	for(unsigned int i=0;i<r->allowed_sites.size();i++)
	{
		sprintf( temp, "user.allowed%d", i+1 );
		listbuffadd( buff, temp, pos, size );
	}
	
	for(unsigned int i=0;i<r->denied_sites.size();i++)
	{
		sprintf( temp, "user.denied%d", i+1 );
		listbuffadd( buff, temp, pos, size );
	}
	
	listbuffadd( buff, "user.allow_all_apps", pos, size );
	
	for(unsigned int i=0;i<r->allowed_applications.size();i++)
	{
		sprintf( temp, "user.app%d", i+1 );
		listbuffadd( buff, temp, pos, size );
	}
	
	return pos;
}

int DRM::GetXAttr( File *file, const char *name, char *value, size_t size )
{
	Rights *r = _Default;
	if ( _ManagedFiles.count(file) > 0 )
		r = &_ManagedFiles[file];
	
	if ( !r )
		return -EFAULT;
	
	if ( !strcmp( name, "user.owner-perm" ) )
	{
		return snprintf( value, size, "%d", r->ownerperms );
	}
	else if ( !strcmp( name, "user.anon-perm" ) )
	{
		return snprintf( value, size, "%d", r->others );
	}
	else if ( !strcmp( name, "user.allow_all_apps" ) )
	{
		return snprintf( value, size, "%d", (int)r->allow_all_apps );
	}
	else if ( !strcmp( name, "user.order_deny_allow" ) )
	{
		return snprintf( value, size, "%d", (int)r->order_deny_allow );
	}
	else if ( strstr( name, "user.group-perm" ) )
	{
		unsigned int g = atoi( &name[15] );
		if ( g <= 0 || g > r->groups.size() )
			return -EINVAL;
		
		unsigned int count = 1;
		for ( GroupMap::iterator iter = r->groups.begin(); iter != r->groups.end(); iter++, count++ )
		{
			if ( count == g )
				return snprintf( value, size, "%d", (int)iter->second );
		}
		
		return 0;
	}
	else if ( strstr( name, "user.group" ) )
	{
		unsigned int g = atoi( &name[10] );
		if ( g <= 0 || g > r->groups.size() )
			return -EINVAL;
		
		unsigned int count = 1;
		for ( GroupMap::iterator iter = r->groups.begin(); iter != r->groups.end(); iter++, count++ )
		{
			if ( count == g )
				return snprintf( value, size, "%s", iter->first->group_name.c_str() );
		}
		
		return 0;
	}
	else if ( strstr( name, "user.owner" ) )
	{
		unsigned int o = atoi( &name[10] ) - 1;
		if ( o < 0 || o >= r->owners.size() )
			return -EINVAL;
		
		return snprintf( value, size, "%s", r->owners[o]->name.c_str() );
	}
	else if ( strstr( name, "user.allowed" ) )
	{
		unsigned int i = atoi( &name[12] ) - 1;
		if ( i < 0 || i >= r->allowed_sites.size() )
			return -EINVAL;
		
		return snprintf( value, size, "%s", r->allowed_sites[i].IPToString().c_str() );
	}
	else if ( strstr( name, "user.denied" ) )
	{
		unsigned int i = atoi( &name[11] ) - 1;
		if ( i < 0 || i >= r->denied_sites.size() )
			return -EINVAL;
		
		return snprintf( value, size, "%s", r->denied_sites[i].IPToString().c_str() );
	}
	else if ( strstr( name, "user.app" ) )
	{
		unsigned int i = atoi( &name[8] ) - 1;
		if ( i < 0 || i >= r->allowed_applications.size() )
			return -EINVAL;
		
		return snprintf( value, size, "%s", r->allowed_applications[i].ToString().c_str() );
	}
	else
	{
		return -ENOATTR;
	}
}

int DRM::SetXAttr( File *file, const char *name, const char *value, size_t size, int flags )
{
	Rights *r = _Default;
	if ( _ManagedFiles.count(file) > 0 )
		r = &_ManagedFiles[file];
	
	if ( !r )
		return -EFAULT;
	
	if ( !strcmp( name, "user.owner-perm" ) )
	{
		if ( flags == XATTR_CREATE )
			return -EEXIST;
		
		r->ownerperms = atoi( value );
		return 0;
	}
	else if ( !strcmp( name, "user.anon-perm" ) )
	{
		if ( flags == XATTR_CREATE )
			return -EEXIST;
		
		r->others = atoi( value );
		return 0;
	}
	else if ( !strcmp( name, "user.allow_all_apps" ) )
	{
		if ( flags == XATTR_CREATE )
			return -EEXIST;
		
		r->allow_all_apps = atoi( value ) == 1;
		return 0;
	}
	else if ( !strcmp( name, "user.order_deny_allow" ) )
	{
		if ( flags == XATTR_CREATE )
			return -EEXIST;
		
		r->order_deny_allow = atoi( value ) == 1;
		return 0;
	}
	else if ( strstr( name, "user.group-perm" ) )
	{
		unsigned int g = atoi( &name[15] );
		if ( g <= 0 || flags == XATTR_CREATE )
			return -EINVAL;
		
		unsigned int count = 1;
		for ( GroupMap::iterator iter = r->groups.begin(); iter != r->groups.end(); iter++, count++ )
		{
			if ( count == g )
			{
				iter->second = atoi( value );
				return 0;
			}
		}
		
		return -ENOATTR;
	}
	else if ( strstr( name, "user.group" ) )
	{
		Group *grp = GetGroup( value );
		if ( !grp )
			return -EINVAL;
		else if ( flags == XATTR_REPLACE )
			return -ENOATTR;
		
		unsigned int g = atoi( &name[10] );
		
		unsigned int count = 1;
		for ( GroupMap::iterator iter = r->groups.begin(); iter != r->groups.end(); iter++, count++ )
		{
			if ( count == g )
			{
				if ( flags == XATTR_CREATE )
					return -EEXIST;
				
				GroupMap::value_type ins( grp, iter->second );
				
				r->groups.erase( iter );
				r->groups.insert( ins );
				
				return 0;
			}
		}
		
		
		if ( flags != XATTR_REPLACE )
		{
			r->groups.insert( GroupMap::value_type( grp, r->ownerperms ) );
			return 0;
		}
		else
		{
			return -ENOATTR;
		}
	}
	else if ( strstr( name, "user.owner" ) )
	{
		unsigned int o = atoi( &name[10] ) - 1;
		if ( o <= 0 )
			return -EINVAL;
		
		User *u = GetUser( value );
		if ( !u )
			return -EINVAL;
		
		if ( o < r->owners.size() )
		{
			if ( flags != XATTR_CREATE )
			{
				r->owners[o] = u;
				return 0;
			}
			else
			{
				return -EEXIST;
			}
		}
		else
		{
			if ( flags != XATTR_REPLACE )
			{
				r->owners.push_back( u );
				return 0;
			}
			else
			{
				return -ENOATTR;
			}
		}
	}
	else if ( strstr( name, "user.allowed" ) )
	{
		unsigned int i = atoi( &name[12] ) - 1;
		if ( i <= 0 )
			return -EINVAL;
		
		in_addr_t ip = inet_addr( value );
		
		if ( ip == INADDR_NONE )
			return -EINVAL;
		
		if ( i < r->allowed_sites.size() )
		{
			if ( flags != XATTR_CREATE )
			{
				r->allowed_sites[i] = NetAddress( ip, 0 );
				return 0;
			}
			else
			{
				return -EEXIST;
			}
		}
		else
		{
			if ( flags != XATTR_REPLACE )
			{
				r->allowed_sites.push_back( NetAddress( ip, 0 ) );
				return 0;
			}
			else
			{
				return -ENOATTR;
			}
		}
	}
	else if ( strstr( name, "user.denied" ) )
	{
		unsigned int i = atoi( &name[11] ) - 1;
		if ( i <= 0 )
			return -EINVAL;
		
		in_addr_t ip = inet_addr( value );
		
		if ( ip == INADDR_NONE )
			return -EINVAL;
		
		if ( i < r->denied_sites.size() )
		{
			if ( flags != XATTR_CREATE )
			{
				r->denied_sites[i] = NetAddress( ip, 0 );
				return 0;
			}
			else
			{
				return -EEXIST;
			}
		}
		else
		{
			if ( flags != XATTR_REPLACE )
			{
				r->denied_sites.push_back( NetAddress( ip, 0 ) );
				return 0;
			}
			else
			{
				return -ENOATTR;
			}
		}
	}
	else if ( strstr( name, "user.app" ) )
	{
		unsigned int i = atoi( &name[8] ) - 1;
		if ( i <= 0 )
			return -EINVAL;
		
		MD5Hash hash;
		hash.FromString( value );
		
		if ( i < r->allowed_applications.size() )
		{
			if ( flags != XATTR_CREATE )
			{
				r->allowed_applications[i] = hash;
				return 0;
			}
			else
			{
				return -EEXIST;
			}
		}
		else
		{
			if ( flags != XATTR_REPLACE )
			{
				r->allowed_applications.push_back( hash );
				return 0;
			}
			else
			{
				return -ENOATTR;
			}
		}
	}
	else
	{
		return -ENOATTR;
	}
}

int DRM::RemoveXAttr( File *file, const char *name )
{
	Rights *r = _Default;
	if ( _ManagedFiles.count(file) > 0 )
		r = &_ManagedFiles[file];
	
	if ( !r )
		return -EFAULT;
	
	if ( !strcmp( name, "user.owner-perm" ) || !strcmp( name, "user.anon-perm" ) || !strcmp( name, "user.allow_all_apps" ) || !strcmp( name, "user.order_deny_allow" ) || strstr( name, "user.group-perm" ) )
	{
		return -EEXIST;
	}
	else if ( strstr( name, "user.group" ) )
	{
		unsigned int g = atoi( &name[10] );
		if ( g <= 0 || g > r->groups.size() )
			return -ENOATTR;
		
		unsigned int count = 1;
		for ( GroupMap::iterator iter = r->groups.begin(); iter != r->groups.end(); iter++, count++ )
		{
			if ( count == g )
			{
				r->groups.erase( iter );
				return 0;
			}
		}
		
		return -ENOATTR;
	}
	else if ( strstr( name, "user.owner" ) )
	{
		unsigned int o = atoi( &name[10] ) - 1;
		if ( o < 0 || o >= r->owners.size() )
			return -ENOATTR;
		
		r->owners.erase( r->owners.begin() + o );
		return 0;
	}
	else if ( strstr( name, "user.allowed" ) )
	{
		unsigned int i = atoi( &name[12] ) - 1;
		if ( i < 0 || i >= r->allowed_sites.size() )
			return -ENOATTR;
		
		r->allowed_sites.erase( r->allowed_sites.begin() + i );
		return 0;
	}
	else if ( strstr( name, "user.denied" ) )
	{
		unsigned int i = atoi( &name[11] ) - 1;
		if ( i < 0 || i >= r->denied_sites.size() )
			return -ENOATTR;
		
		r->denied_sites.erase( r->denied_sites.begin() + i );
		return 0;
	}
	else if ( strstr( name, "user.app" ) )
	{
		unsigned int i = atoi( &name[8] ) - 1;
		if ( i < 0 || i >= r->allowed_applications.size() )
			return -ENOATTR;
		
		r->allowed_applications.erase( r->allowed_applications.begin() + i );
		return 0;
	}
	else
	{
		return -ENOATTR;//-EINVAL;
	}
}

int DRM::GetPerms(FSObject *fsobj)
{
	if (_ManagedFiles.count(fsobj) > 0)
	{
		Rights file_rights = _ManagedFiles[fsobj];
		//Check for ownership
		if (_Curr != NULL)
		{
			for (unsigned int i = 0; i < file_rights.owners.size(); i++)
			{
				if (file_rights.owners[i] == _Curr)
					return file_rights.ownerperms;
			}
		}
		
		int perms = file_rights.others;
		cout << "Checking groups..." << endl;
		//Sum all possible rights to the file
		for (unsigned int i = 0; i < _CurrGroups.size(); i++)
		{			
			cout << "Checking group " << _CurrGroups[i]->group_name << endl;
			//If file gives perm to that group...
			GroupMap::iterator iter = file_rights.groups.find( _CurrGroups[i] );
			if ( iter != file_rights.groups.end() )
			{
				cout << "Old: " << perms << ", given " << iter->second;
				perms = AddPerms(perms, iter->second);
				cout << ", new " << perms << endl;
			}
			else
			{
				cout << "NEGATIVE!" << endl;
			}
		}
		
		return perms;
	}
	
	return 0;
}

void DRM::AddUser(string name, string password)
{
	//Exhaustive check of all current users
	for (unsigned int i = 0; i < users.size(); i++)
	{
		if (users[i]->name == name)
			return;
	}
	
	User *new_user = new User;
	new_user->name = name;
	new_user->passwd = password;
	
	users.push_back(new_user); 
}


void DRM::AddFile(FSObject *file)
{
	if (_Default != NULL)
	{
		RightsMap::iterator iter = _ManagedFiles.find(file);
		if (iter == _ManagedFiles.end())
			_ManagedFiles.insert(RightsMap::value_type(file, *_Default));
	}
}

bool DRM::CanRead(FSObject *fsobj)
{
	int perms = GetPerms(fsobj);	
	if (perms&1)
		return true;
		
	return false;
}

bool DRM::CanWrite(FSObject *fsobj)
{
	int perms = GetPerms(fsobj);	
	if (perms&4)
		return true;
		
	return false;
}

bool DRM::CanAppend(FSObject *fsobj)
{
	int perms = GetPerms(fsobj);	
	if (perms&2)
		return true;
		
	return false;
}

bool DRM::CanRemove(FSObject *fsobj)
{
	int perms = GetPerms(fsobj);	
	if (perms&8)
		return true;
		
	return false;
}


bool DRM::CanAppRead( const MD5Hash &app, File *file )
{
	// check "app" against allowed hashes stored in the DRM for "file"
	
	Rights *r = _Default;
	if ( _ManagedFiles.count(file) > 0 )
		r = &_ManagedFiles[file];
	
	if ( !r || r->allow_all_apps )
		return true;
	
	for(unsigned int i=0;i<r->allowed_applications.size();i++)
	{
		if ( r->allowed_applications[i] == app )
			return true;
	}
	
	return false;
}

bool DRM::CanAppWrite( const MD5Hash &app, File *file )
{
	return CanAppRead( app, file ); 
}

bool DRM::IsSiteAllowed( FSObject *fsobj, const NetAddress &addr )
{
	Rights *r = _Default;
	if (_ManagedFiles.count(fsobj) > 0)
		r = &_ManagedFiles[fsobj];
	
	if ( r == NULL )
		return false;
	
	for(unsigned int i=0;i<r->allowed_sites.size();i++)
	{
		if ( r->allowed_sites[i].IP() == addr.IP() )
			return true;
	}
	
	for(unsigned int i=0;i<r->denied_sites.size();i++)
	{
		if ( r->denied_sites[i].IP() == addr.IP() )
			return false;
	}
	
	return !r->order_deny_allow;
}

bool DRM::IsOwner( FSObject *fsobj )
{
	Rights *r = _Default;
	if (_ManagedFiles.count(fsobj) > 0)
		r = &_ManagedFiles[fsobj];
	
	//Check for ownership
	if ( _Curr != NULL && r != NULL )
	{
		for (unsigned int i = 0; i < r->owners.size(); i++)
		{
			if (r->owners[i] == _Curr)
				return true;
		}
	}
	
	return false;
}

//Encryption Functions
void DRM::Encrypt(File *file, Packet &p)
{
	unsigned char out_buffer[1032];
	unsigned char in_buffer[1024];
	int out_len;

	EVP_CIPHER_CTX context;
	EVP_CIPHER_CTX_init ( &context );
	
	//Initialize key and initialization vector.
	unsigned char key[] = { "16 characters..." };
	unsigned char init_vect[] = { "8 chars." };
	
	EVP_EncryptInit(&context, EVP_bf_cbc(), key, init_vect);
	
	unsigned int total_bytes = 0;
	while ( total_bytes < file->_LocalSize )
	{
		bzero(in_buffer, 1024);
		//Read from where we left off last iteration
		
		unsigned int bytes_read = 1024;
		if ( total_bytes + bytes_read > file->_LocalSize )
			bytes_read = file->_LocalSize - total_bytes;
			
		memcpy( in_buffer, &file->_Data[total_bytes], bytes_read );
		total_bytes += bytes_read;
		
		//HexDump( cout, (char *)in_buffer, bytes_read );
		
		if ( bytes_read == 0 ) 
		{
			cerr << "Reading Error in encryption!" << endl;
			EVP_CIPHER_CTX_cleanup( &context );
			return;
		} 
		else 
		{
			total_bytes += bytes_read;
		}
		
		out_len = 1032;
		if ( !EVP_EncryptUpdate(&context, out_buffer, &out_len, in_buffer, bytes_read) )
		{
			cerr << "Error in encrypt update!" << endl;
			EVP_CIPHER_CTX_cleanup( &context );
			return;
		}

		//HexDump( cout, (char *)out_buffer, out_len);
		
		//bytes_wrote = file->Write(out_buffer, out_len, total_bytes_wrote);
		//if (bytes_wrote == 0) cerr << "Write freaked out..." << endl;
		//else total_bytes_wrote += bytes_wrote;
		p.WriteShort(out_len);
		p.WriteRaw(out_buffer, out_len);
	}
	
	out_len = 1032;
	if ( !EVP_EncryptFinal(&context, out_buffer, &out_len) )
	{
		cerr << "Error in Final Encrypt!" << endl;
		return;
	}
	
	if ( out_len > 0 )
	{
		//HexDump( cout, (char *)out_buffer, out_len);
		
		p.WriteShort(out_len);
		p.WriteRaw(out_buffer, out_len);
	}
}

void DRM::Decrypt(File *file, PacketReader &reader)
{
	unsigned char out_buffer[1024];
	unsigned char in_buffer[1032];
	int out_len;	
	
	EVP_CIPHER_CTX context;
	EVP_CIPHER_CTX_init(&context);
	
	//Initialize key and initialization vector.
	unsigned char key[] = { "16 characters..." };
	unsigned char init_vect[] = { "8 chars." };
	
	EVP_DecryptInit(&context, EVP_bf_cbc(), key, init_vect);

	unsigned int total_bytes = 0;
	while ( !reader.AtEnd() && total_bytes < file->_LocalSize )
	{
		bzero(in_buffer, 1032);
		
		int in_len = reader.ReadShort();
		
		if (!reader.ReadRaw(in_buffer, in_len))
		{
			cerr << "Reading error during decryption!" << endl;
			EVP_CIPHER_CTX_cleanup( &context );
			return;
		}
		
		bzero (out_buffer, 1024);
		out_len = 1024;
		if (!EVP_DecryptUpdate(&context, out_buffer, &out_len, in_buffer, in_len))
		{
			cerr << "Error in decrypt update!" << endl;
			EVP_CIPHER_CTX_cleanup( &context );
			return;
		}
		
		if ( out_len > 0 )
		{
			memcpy( &file->_Data[total_bytes], out_buffer, out_len );
			total_bytes += out_len;
		}
	}
	
	out_len = 1024;
	if ( !EVP_DecryptFinal(&context, out_buffer, &out_len) )
	{
		cerr << "Error in decrypt final!" << endl;
		return;
	}
	
	if ( out_len > 0 )
	{
		memcpy( &file->_Data[total_bytes], out_buffer, out_len);
		total_bytes += out_len;
	}
	
	//cout << "After decrypt... " << file->Name() << ": LocalSize: " << file->_LocalSize << ", decsize: " << total_bytes_wrote << endl;
}

int DRM::AddPerms(int perm1, int perm2)
{
	return perm1|perm2;
}

DRM::User* DRM::GetUser(string username)
{
	for (unsigned int i = 0;i < users.size();i++)
	{
		if (users[i]->name == username)
			return users[i];
	}
	
	return NULL;
}

DRM::Group* DRM::GetGroup(string gname)
{
	for (unsigned int i = 0;i < groups.size();i++)
	{
		if (groups[i]->group_name == gname)
			return groups[i];
	}
	
	return NULL;
}

vector<string> DRM::split(string str, char c)
{
	vector<string> result;
	string tmp = "";
	for (unsigned int i = 0; i < str.length();i++)
	{
		if (str[i] == c)
		{
			result.push_back(tmp);
			tmp = "";
			continue;
		}
		tmp += str[i];
	}
	if (tmp != "")
		result.push_back(tmp);
	
	return result;
}

