/*
Copyright (C) 2013 Victor Luchits

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

/*

Paul Hsieh Hash Function
Version: None specified

Vendor: Paul Hsieh

Paul Hsieh OLD BSD license

Copyright © 2010, Paul Hsieh All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

Neither my name, Paul Hsieh, nor the names of any other contributors to the code use may not be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE

*/

#include "hash.h"

/*
* COM_HashKey
* 
* Returns hash key for a string
*/
unsigned int COM_HashKey( const char *name, int hashsize )
{
	int i;
	unsigned int v;
	unsigned int c;

	v = 0;
	for( i = 0; name[i]; i++ )
	{
		c = name[i];
		if( c == '\\' )
			c = '/';
		v = ( v + i ) * 37 + tolower( c ); // case insensitivity
	}

	return v % hashsize;
}

#undef get16bits
#if (defined(__GNUC__) && defined(__i386__)) || defined(__WATCOMC__) \
  || defined(_MSC_VER) || defined (__BORLANDC__) || defined (__TURBOC__)
#define get16bits(d) (*((const unsigned short *) (d)))
#endif

#if !defined (get16bits)
#define get16bits(d) ((((unsigned int)(((const unsigned char *)(d))[1])) << 8)\
                       +(unsigned int)(((const unsigned char *)(d))[0]) )
#endif

#define Com_SuperFastHash_Avalanche(hash) \
    hash ^= hash << 3; \
    hash += hash >> 5; \
    hash ^= hash << 4; \
    hash += hash >> 17; \
    hash ^= hash << 25; \
    hash += hash >> 6;

/*
* COM_SuperFastHash
* 
* Adaptation of Paul Hsieh's incremental SuperFastHash function.
* Initialize hash to some non-zero value for the first call, like len.
*/
unsigned int COM_SuperFastHash( const uint8_t * data, size_t len, unsigned int hash )
{
	unsigned int tmp;
	unsigned int rem;

	if( len <= 0 || data == NULL ) {
		return 0;
	}

    rem = len & 3;
    len >>= 2;

    // main loop
    for( ; len > 0; len-- ) {
        hash  += get16bits (data);
        tmp    = (get16bits (data+2) << 11) ^ hash;
        hash   = (hash << 16) ^ tmp;
        data  += 2*sizeof (unsigned short);
        hash  += hash >> 11;
    }

    // Handle end cases
    switch (rem) {
        case 3: hash += get16bits (data);
                hash ^= hash << 16;
                hash ^= data[sizeof (unsigned short)] << 18;
                hash += hash >> 11;
                break;
        case 2: hash += get16bits (data);
                hash ^= hash << 11;
                hash += hash >> 17;
                break;
        case 1: hash += *data;
                hash ^= hash << 10;
                hash += hash >> 1;
    }

    // Force "avalanching" of final 127 bits
	Com_SuperFastHash_Avalanche( hash );

    return hash;
}

/*
* COM_SuperFastHash64BitInt
* 
* Com_SuperFastHash that takes a 64bit integer as an argument
*/
unsigned int COM_SuperFastHash64BitInt( uint64_t data )
{
	unsigned int hash;
	unsigned int il, ih;
	unsigned int tmp;

	hash = sizeof( data );

	// split into two
	il = data & ULONG_MAX;
	ih = (data >> 32) & ULONG_MAX;

	hash += (il & 0xFFFF);
	tmp = (((il >> 16) & 0xFFFF) << 11) ^ hash;
	hash = (hash << 16) ^ tmp;
	hash += hash >> 11;

	hash += (ih & 0xFFFF);
	tmp = (((ih >> 16) & 0xFFFF) << 11) ^ hash;
	hash = (hash << 16) ^ tmp;
	hash += hash >> 11;

	// Force "avalanching" of final 127 bits
	Com_SuperFastHash_Avalanche( hash );

	return hash;
}
