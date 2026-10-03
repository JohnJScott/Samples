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
#include "slidey.h"

// the nested arrays used to lookup the attacks
std::array<std::array<uint64_t, 512>, 64> bishopAttackLookup;
std::array<std::array<uint64_t, 4096>, 64> rookAttackLookup;

static int32_t getBishopBlockerCombinations( std::array<uint64_t, 512>& blockers, const uint64_t mask )
{
	// calculates the total amount of possible blocker configuration (2^n)
	int32_t totalPatterns = 1 << std::popcount( mask );
	assert( totalPatterns <= 512 );

	// generates them, using pdep
	for( int32_t i = 0; i < totalPatterns; i++ )
	{
		blockers[i] = _pdep_u64( i, mask );
	}

	return totalPatterns;
}

// seperated so that I can have the assertions be right
static int32_t getRookBlockerCombinations( std::array<uint64_t, 4096>& blockers, const uint64_t mask )
{
	// calculates the total amount of possible blocker configuration (2^n)
	int32_t totalPatterns = 1 << std::popcount( mask );
	assert( totalPatterns <= 4096 );

	// generates them, using pdep
	for( int32_t i = 0; i < totalPatterns; i++ )
	{
		blockers[i] = _pdep_u64( i, mask );
	}

	return totalPatterns;
}

// generates the lookup tables for each square, ran on startup
void generateLookups()
{
	// loops through each square
	for( int32_t i = 0; i < 64; i++ )
	{
		// generate the different blocker patterns
		std::array<uint64_t, 4096> rookBlockers;
		int32_t rookPatterns = getRookBlockerCombinations( rookBlockers, rookMasks[i] );

		// calculate the moves for each one
		for( int32_t j = 0; j < rookPatterns; j++ )
		{
			rookAttackLookup[i][_pext_u64( rookBlockers[j], rookMasks[i] )] = getRookAttacksOld( i, rookBlockers[j] );
		}

		// bishop blocker patterns
		std::array<uint64_t, 512> bishopBlockers;
		int32_t bishopPatterns = getBishopBlockerCombinations( bishopBlockers, bishopMasks[i] );

		// calculate the moves for each one
		for( int32_t j = 0; j < bishopPatterns; j++ )
		{
			bishopAttackLookup[i][_pext_u64( bishopBlockers[j], bishopMasks[i] )] = getBishopAttacksOld( i, bishopBlockers[j] );
		}
	}
}

