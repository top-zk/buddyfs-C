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

#ifndef __MUTEX_H_
#define __MUTEX_H_

#include <pthread.h>

class Mutex
{
public:
	Mutex() { pthread_mutex_init( &_Mutex, NULL ); }
	virtual ~Mutex() { pthread_mutex_destroy( &_Mutex ); }

	bool TryLock() { return pthread_mutex_trylock( &_Mutex ) == 0; }
	void Lock() { pthread_mutex_lock( &_Mutex ); }
	void Unlock() { pthread_mutex_unlock( &_Mutex ); }
	
	pthread_mutex_t *Handle() { return &_Mutex; }

private:
	pthread_mutex_t _Mutex;
};

#endif
