/*
	Clarity
	Copyright (C) 2026 Joseph Pasfield

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "tt.h"

// all the functions for the transposition table

static inline uint64_t index( uint64_t a, uint64_t b )
{
#if defined(_MSC_VER) && defined(_M_X64)
	return __umulh( a, b );
#else
	return static_cast< uint64_t >( ( static_cast< unsigned __int128 >( a ) * static_cast< unsigned __int128 >( b ) ) >> 64 );
#endif
}

Transposition* TranspositionTable::getEntry( uint64_t zkey )
{
	return &table[index( zkey, size )];
}

void TranspositionTable::setEntry( uint64_t zkey, Transposition& entry )
{
	table[index( zkey, size )] = entry;
}

void TranspositionTable::clearTable( int32_t threadCount )
{
	const uint64_t chunks = ( size + threadCount - 1 ) / threadCount;
	std::vector<std::thread> threads;
	for( int32_t i = 0; i < threadCount; ++i )
	{
		threads.emplace_back( [i, chunks, this]()
		{
			const uint64_t clearIndex = chunks * i;
			const uint64_t clearSize = std::min( chunks, size - clearIndex ) * sizeof( Transposition );
			std::memset( table.data() + clearIndex, 0, clearSize );
		} );
	}

	for( std::thread& thread : threads )
	{
		thread.join();
	}
}

void TranspositionTable::resize( uint64_t newSizeMB, int32_t threadCount )
{
	uint64_t newSizeB = newSizeMB * 1024 * 1024;
	uint64_t newSizeEntries = newSizeB / sizeof( Transposition );
	size = newSizeEntries;
	table.resize( size, Transposition() );
	clearTable( threadCount );
}
