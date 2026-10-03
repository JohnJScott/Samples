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
#include "testessentials.h"
#include "uci.h"

// runs a single perft test
int32_t perft( Board& board, int32_t depth )
{
	if( depth == 0 )
	{
		return 1;
	}

	std::array<Move, 256> moves;
	int32_t numMoves = board.getMoves( moves );
	int32_t result = 0;
	for( int32_t i = 0; i < numMoves; i++ )
	{
		if( board.makeMove<false, false>( moves[i] ) )
		{
			result += perft( board, depth - 1 );
			board.undoMove<false, false>();
		}
	}

	return result;
}

// runs perft split by what the first move that is done is
void splitPerft( Board board, int32_t depth )
{
	std::array<Move, 256> moves;
	int32_t numMoves = board.getMoves( moves );
	int64_t total = 0;
	clock_t start = clock();
	for( int32_t i = 0; i < numMoves; i++ )
	{
		if( board.makeMove<false, false>( moves[i] ) )
		{
			int32_t result = perft( board, depth - 1 );
			board.undoMove<false, false>();
			total += result;
			std::cout << toLongAlgebraic( moves[i] ) << ": " << std::to_string( result ) << '\n';
		}
	}

	clock_t end = clock();
	double seconds = ( end - start ) / 1000.0;
	
	std::cout << "Total: " << std::to_string( total ) << '\n';
	std::cout << "Time: " << std::to_string( seconds ) << '\n';
	std::cout << "NPS: " << std::to_string( static_cast<double>( total ) / seconds ) << '\n';
}

// runs an individual perft test, and outputs the results
void individualPerft( Board board, int32_t depth )
{
	clock_t start = clock();
	int32_t result = perft( board, depth );
	clock_t end = clock();
	double seconds = ( end - start ) / 1000.0;

	std::cout << "Result: " << std::to_string( result ) << '\n';
	std::cout << "Time: " << std::to_string( seconds ) << '\n';
	std::cout << "NPS: " << std::to_string( result / seconds ) << '\n';
}