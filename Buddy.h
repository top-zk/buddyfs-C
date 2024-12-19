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

#ifndef __BUDDY_H_
#define __BUDDY_H_

#include <map>
#include <list>
#include <iostream>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>

using namespace std;

#define MAX_PATH 512

class Socket;
class NetAddress;
class Clique;
class AlphaClique;

class FileSystem;
class File;
class Folder;
class FSObject;

typedef std::list<NetAddress> AddressList;
typedef std::map<NetAddress, Socket *> PeerMap;
typedef std::pair<NetAddress, Socket *> Peer;

Socket *FindPeer( const NetAddress &addr );

extern unsigned short LocalPort;
extern const char *BuddyDir;
extern PeerMap Peers;
extern AlphaClique Alpha;

#include "Socket.h"
#include "Clique.h"

#endif
