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

#include <netinet/in.h>
#include <string.h>
#include <memory.h>

#include "Buddy.h"
#include "Packet.h"

void HexDump( ostream &out, const char *data, int len );

Packet::Packet( int cmd, int reqID, int len )
{
	if ( len < PAYLOAD_BEGIN )
		len = 64;
	
	_Buff = new char[len];
	
	_Buff[0] = (char)cmd;
	_MaxLen = len;
	_Len = _Pos = PAYLOAD_BEGIN - 4;
	
	if ( reqID == 0 )
		WriteInt( rand() );
	else
		WriteInt( reqID );
}

Packet::Packet( const Packet &cpy )
{
	_MaxLen = cpy._MaxLen;
	_Len = cpy._Len;
	_Pos = cpy._Pos;
	
	_Buff = new char[_MaxLen];

	memcpy( _Buff, cpy._Buff, _Len );
}

Packet::~Packet()
{
	delete[] _Buff;
}

const Packet &Packet:: operator = ( const Packet &cpy )
{
	if ( this != &cpy )
	{
		delete[] _Buff;
		
		_MaxLen = cpy._MaxLen;
		_Len = cpy._Len;
		_Pos = cpy._Pos;
	
		_Buff = new char[_MaxLen];

		memcpy( _Buff, cpy._Buff, _Len );
	}
	
	return *this;
}

const char *Packet::Buffer()
{
	int oldPos = _Pos;
	_Pos = 1;
	WriteInt( _Len );
	_Pos = oldPos;

	return _Buff;
}

PacketReader Packet::MakeReader()
{
	return PacketReader( Buffer() );
}

void Packet::WriteRaw( const void *ptr, int len )
{
	PreWrite( len );
	
	memcpy( &_Buff[_Pos], ptr, len );
	_Pos += len;
}

void Packet::WriteASCII( const char *ascii )
{
	WriteRaw( ascii, strlen( ascii )+1 );
}

void Packet::WriteAddress( const NetAddress &addr )
{
	WriteInt( addr.IP() );
	WriteShort( addr.Port() );
}

void Packet::WriteInt( int val )
{
	PreWrite( 4 );

	*((unsigned int *)&_Buff[_Pos]) = htonl( (unsigned int)val );
	_Pos += 4;
}

void Packet::WriteUnsignedInt( unsigned int val )
{
	PreWrite( 4 );

	*((unsigned int *)&_Buff[_Pos]) = htonl( val );
	_Pos += 4;
}

void Packet::WriteShort( short val )
{
	PreWrite( 2 );

	*((unsigned short *)&_Buff[_Pos]) = htons( (unsigned short)val );
	_Pos += 2;
}

void Packet::WriteByte( char val )
{
	PreWrite( 1 );

	_Buff[_Pos] = val;
	_Pos++;
}

void Packet::EnsureCapacity( int cap )
{
	int needed = cap - _MaxLen;

	if ( needed > 0 )
	{
		if ( needed < 64 )
			needed = 64;

		_MaxLen += needed;
		
		char *old = _Buff;
		
		_Buff = new char[_MaxLen];
		
		if ( old && _Len )
		{
			memcpy( _Buff, old, _Len );
			delete[] old;
		}
	}
}

void Packet::PreWrite( int size )
{
	int needed = ( _Pos + size ) - _MaxLen;

	if ( needed > 0 )
	{
		char *old = _Buff;

		if ( needed < 64 )
			needed = 64;

		_MaxLen += needed;

		_Buff = new char[_MaxLen];
		memcpy( _Buff, old, _Len );

		delete[] old;
	}

	if ( _Len < _Pos + size )
		_Len = _Pos + size;
}

ostream &operator << ( ostream &out, const Packet &p )
{
	char buffer[80];
		
	sprintf( buffer, "Packet[0x%02X,%4d,0x%08X]", (int)((unsigned char)p._Buff[0]),  p.Length()-Packet::PAYLOAD_BEGIN, p.RequestID() );
	out << buffer << endl;
		
	HexDump( out, &p._Buff[Packet::PAYLOAD_BEGIN], p._Len - Packet::PAYLOAD_BEGIN );
		
	return out;
}










PacketReader::PacketReader( const char *buff ) : _Buff( NULL ), _Len( 0 ), _Pos( PAYLOAD_BEGIN )
{
	if ( buff )
	{
		_Len = ntohl( *((unsigned int *)&buff[1]) );
		
		_Buff = new char[_Len];
			
		memcpy( _Buff, buff, _Len );
	}
}

PacketReader::PacketReader( char *buff, int len ) : _Buff( buff ), _Len( len ), _Pos( PAYLOAD_BEGIN )
{
}

PacketReader::PacketReader( const PacketReader &cpy ) : _Buff( NULL ), _Len( cpy._Len ), _Pos( cpy._Pos )
{
	_Buff = new char[_Len];

	memcpy( _Buff, cpy._Buff, _Len );
}

PacketReader::~ PacketReader()
{
	delete[] _Buff;
}

const PacketReader &PacketReader:: operator = ( const PacketReader &copy )
{
	if ( &copy != this )
	{
		delete[] _Buff;
		
		_Len = copy._Len;
		_Pos = copy._Pos;
		if ( copy._Buff && _Len > 0 )
		{
			_Buff = new char[_Len];
			memcpy( _Buff, copy._Buff, _Len );
		}
		else
		{
			_Buff = NULL;
		}
	}
	
	return *this;
}

bool PacketReader::IsValid() const
{
	return _Buff != NULL && _Len >= PAYLOAD_BEGIN && _Len <= 0x4000;
}

Packet PacketReader::MakePacket()
{
	Packet p( Command(), RequestID(), Length() );
	
	if ( Length() > PAYLOAD_BEGIN )
		p.WriteRaw( &_Buff[PAYLOAD_BEGIN], Length() - PAYLOAD_BEGIN );
	
	return p;
}

int PacketReader::ReadASCII( char *buff, int max )
{
	int p;
	
	for( p = 0; !AtEnd(); _Pos++, p++ )
	{
		buff[p] = _Buff[_Pos];
		if ( _Buff[_Pos] == 0 )
		{
			_Pos++;
			break;
		}
		else if ( p+1 >= max )
		{
			p = max - 1;
			break;
		}
	}
	
	buff[p] = 0;

	return p;
}

bool PacketReader::ReadRaw( void *ptr, int size )
{
	if ( _Pos + size > _Len )
		return false;

	memcpy( ptr, &_Buff[_Pos], size );
	_Pos += size;

	return true;
}

NetAddress PacketReader::ReadAddress()
{
	in_addr_t ip = ReadInt();
	unsigned short port = (unsigned short)ReadShort();
		
	return NetAddress( ip, port );
}

int PacketReader::ReadInt()
{
	if ( _Pos + 4 > _Len )
		return -1;
	
	int val = (int)ntohl( *((unsigned int *)&_Buff[_Pos]) );

	_Pos += 4;

	return val;
}

unsigned int PacketReader::ReadUnsignedInt()
{
	if ( _Pos + 4 > _Len )
		return (unsigned)-1;
	
	unsigned int val = (unsigned int)ntohl( *((unsigned int *)&_Buff[_Pos]) );

	_Pos += 4;

	return val;
}

short PacketReader::ReadShort()
{
	if ( _Pos + 2 > _Len )
		return -1;

	short val = (short)ntohs( *((unsigned short*)&_Buff[_Pos]) );

	_Pos += 2;

	return val;
}

char PacketReader::ReadByte()
{
	if ( _Pos >= _Len )
		return -1;

	return _Buff[_Pos++];
}

ostream &operator << ( ostream &out, const PacketReader &p )
{
	char buffer[80];
	
	sprintf( buffer, "PacketReader[0x%02X,%4d,0x%08X]", (int)((unsigned char)p._Buff[0]),  p.Length()-PacketReader::PAYLOAD_BEGIN, p.RequestID() );
	out << buffer << endl;
		
	HexDump( out, &p._Buff[PacketReader::PAYLOAD_BEGIN], p._Len - PacketReader::PAYLOAD_BEGIN );
		
	return out;
}

void HexDump( ostream &out, const char *data, int len )
{
	char temp[4];

	if ( len > 16 )
		out << "{" << endl << "   ";
	else
		out << "{  ";

	if ( len > 64  )
		len = 64;

	for( int i = 0; i < len; i += 16 )
	{
		for(int j=0;j<16;j++)
		{
			if ( j%8 == 0 )
				out << " ";

			if ( i+j < len )
			{
				sprintf( temp, "%02X", (unsigned char)data[i+j] );
				out << temp << " ";
			}
			else
			{
				out << "   ";
			}
		}

		out << "  ";

		for(int j=0; j<16 && i+j<len; j++)
		{
			if ( j%8 == 0 )
				out << " ";

			if ( data[i+j] >= 32 && data[i+j] <= 126 )
				out << data[i+j];
			else
				out << '.';
		}

		if ( i+16 < len )
			out << endl << "   ";
	}

	if ( len > 16 )
		out << endl << "}" << endl;
	else
		out << " }" << endl;
}
