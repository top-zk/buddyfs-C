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

#ifndef __PACKET_H_
#define __PACKET_H_


enum COMMANDS
{
	NOTHING = 0,	// 0x00
	IN_PORT,
	HANDSHAKE,
	HANDSHAKE_RESP,
	LOCAL_FILES,
	PING,
	PONG,
	LIST_REQ,
	
	LIST_RESP,		// 0x08
	CREATE_REQ, 
	CREATE_RESP,
	FS_REQ, 
	FS_RESP, 
	OPEN_REQ, 
	OPEN_RESP,
	READ_REQ,
	
	DATA_BLOCK, 	// 0x10
	FILE_UPDATE,
	RM_DIR,
	RM_FILE,
	FORWARD_REQ,
	DRM_REQ,
	DRM_RESP,
	RENAME,
	
	UPDATE_DRM,		// 0x18
	MAKE_ALPHA,
};
	
class NetAddress;
class Packet;
class PacketReader;

class Packet
{
public:
	static const int PAYLOAD_BEGIN = 1+4+4;
	
	explicit Packet( int cmd, int reqID = 0, int length = 0 );
	Packet( const Packet &cpy );
	virtual ~Packet();
	
	const Packet &operator = ( const Packet &copy );

	int RequestID() const { return ntohl( *((unsigned int *)&_Buff[5]) ); }
	int Length() const { return _Len; }
	int Capacity() const { return _MaxLen; }
	const char *Buffer();

	PacketReader MakeReader();
	
	int Tell() const { return _Pos; }
	void Seek( int pos ) { _Pos = pos; }

	void WriteRaw( const void *ptr, int size );
	void WriteASCII( const char *ascii );
	void WriteAddress( const NetAddress &addr );
	void WriteInt( int );
	void WriteUnsignedInt( unsigned int );
	void WriteShort( short );
	void WriteByte( char );
	void WriteBool( bool val ) { WriteByte( val ? 1 : 0 ); }

	void EnsureCapacity( int cap );
	
	friend ostream &operator << ( ostream &out, const Packet &p );
	
private:
	void PreWrite( int size );
	
	char *_Buff;
	int _MaxLen, _Len, _Pos;
};

class PacketReader
{
public:
	static const int PAYLOAD_BEGIN = Packet::PAYLOAD_BEGIN;
	
	explicit PacketReader( const char *buff );
	explicit PacketReader( char *buff, int len );
	PacketReader( const PacketReader &cpy );
	virtual ~PacketReader();
	
	const PacketReader &operator = ( const PacketReader &copy );

	int Command() const { return (unsigned char)_Buff[0]; }
	int RequestID() const { return (int)ntohl( *((unsigned int *)&_Buff[5]) ); }
	
	NetAddress Sender() const;
	
	int Length() const { return _Len; }

	bool IsValid() const;
	
	Packet MakePacket();
	
	bool AtEnd() const { return _Pos >= _Len; }
	int Tell() const { return _Pos; }
	
	void Seek( int pos ) { _Pos = pos; }

	int ReadASCII( char *buff, int maxSize );
	bool ReadRaw( void *ptr, int size );
	
	NetAddress ReadAddress();
	
	int ReadInt();
	unsigned int ReadUnsignedInt();
	short ReadShort();
	char ReadByte();
	bool ReadBool() { return ReadByte() != 0; }
	
	friend ostream &operator << ( ostream &out, const PacketReader &p );
	
private:
	char *_Buff;
	int _Len, _Pos;
};

#endif
