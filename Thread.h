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

#ifndef __THREAD_H_
#define __THREAD_H_

#include <pthread.h>

class Thread
{	
public:
	Thread() : _Handle( 0 ) { }
	virtual ~Thread()
	{
	}

	void StartThread()
	{
		KillThread();
		
		pthread_create( &_Handle, NULL, ThreadStub, this );
	}
		
	void KillThread()
	{
		if ( _Handle )
		{
			pthread_kill( _Handle, -9 );
			_Handle = 0;
		}
	}

	int JoinThread()
	{
		void *ret;
		pthread_join( _Handle, &ret );

		return (int)ret;
	}

	const pthread_t ThreadHandle() const { return _Handle; }

private:
	virtual int ThreadMain() = 0;

	pthread_t _Handle;
	
	static void *ThreadStub( void *param )
	{
		int ret;
		if ( param )
		{
			ret = ((Thread*)param)->ThreadMain();
			((Thread*)param)->_Handle = 0;
		}
		else
		{
			ret = -1;
		}

		return (void*)ret;
	}
};

#endif
