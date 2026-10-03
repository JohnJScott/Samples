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
#include "tunables.h"
#include "globals.h"

#include <windows.h>

#include "slidey.h"

std::array<std::array<uint8_t, 218>, 150> reductions;
std::array<uint64_t, 64> squareToBitboard;

// converts a move to long algebraic form
std::string toLongAlgebraic( Move move )
{
	std::string longAlgebraic;
	longAlgebraic += squareNames[move.getStartSquare()];
	longAlgebraic += squareNames[move.getEndSquare()];
	switch( move.getFlag() )
	{
	case promotions[0]:
		longAlgebraic += 'n';
		break;
	case promotions[1]:
		longAlgebraic += 'b';
		break;
	case promotions[2]:
		longAlgebraic += 'r';
		break;
	case promotions[3]:
		longAlgebraic += 'q';
		break;
	default:
		break;
	}

	return longAlgebraic;
}

// calculates the reductions used for LMR, ran on startup
void calculateReductions()
{
	for( int32_t depth = 0; depth < 150; depth++ )
	{
		for( int32_t move = 0; move < 218; move++ )
		{
			if( depth == 0 || move == 0 )
			{
				reductions[depth][move] = 0;
				continue;
			}

			reductions[depth][move] = static_cast<uint8_t>( lmrBase.value + log( depth ) * log( move ) * lmrMultiplier.value );
		}
	}
}

static void generateSquareToBitboard()
{
	for( int32_t i = 0; i < 64; i++ )
	{
		squareToBitboard[i] = ( 1ULL << i );
	}
}

/*ran on startup, does 4 things:
1: generates the lookups for sliding pieces
2: generates the numbers used for zobrist hashing
3: calculates the numbers for LMR
4: calculates numbers for square to bitboard lookups
 */
void initialize()
{
	SetConsoleOutputCP( CP_UTF8 );
	SetConsoleCP( CP_UTF8 );

	generateSquareToBitboard();
	generateLookups();
	initializeZobrist();
	calculateReductions();
}

// splits a string into segments based on the seperator
std::vector<std::string> split( const std::string string, const char separator )
{
	std::stringstream stream( string );
	std::string segment;
	std::vector<std::string> list;

	// every time that it can get a segment
	while( std::getline( stream, segment, separator ) )
	{
		// add it to the vector
		list.push_back( segment );
	}

	return list;
}

// sorts the moves all at once
void sortMoves( std::array<int32_t, 256>& values, std::array<Move, 256>& moves, int32_t numMoves )
{
	// for each value
	for( int32_t i = 0; i < numMoves - 1; i++ )
	{
		// find the lowest number that hasn't been sorted yet
		int32_t lowestIndex = i;
		for( int32_t j = i + 1; j < numMoves; j++ )
		{
			if( values[j] < values[lowestIndex] )
			{
				lowestIndex = j;
			}
		}

		// swap the elements
		if( lowestIndex != i )
		{
			std::swap( values[lowestIndex], values[i] );
			std::swap( moves[lowestIndex], moves[i] );
		}
	}
}

