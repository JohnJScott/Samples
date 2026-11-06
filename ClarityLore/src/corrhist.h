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
#pragma once

#include <cmath>

#include "globals.h"
#include "tunables.h"

struct Corrhist
{
	std::array<std::array<int32_t, EColor::ColorCount>, corrHistSize> pawnTable;
	std::array<std::array<std::array<int32_t, EColor::ColorCount>, EColor::ColorCount>, corrHistSize> nonPawnTables;
	std::array<std::array<int32_t, EColor::ColorCount>, corrHistSize> majorTable;
	std::array<std::array<int32_t, EColor::ColorCount>, corrHistSize> minorTable;

	inline int32_t correct( EColor colorToMove, uint32_t pawnHash, int32_t staticEval, const std::array<uint32_t, EColor::ColorCount>& nonPawnHashes, uint32_t majorHash, uint32_t minorHash ) const
	{
		double pawn_correction = ( pawnTable[pawnHash][colorToMove] * pawnChWeight.value ) / 512.0;
		double non_pawn_correction = ( ( nonPawnTables[nonPawnHashes[colorToMove]][colorToMove][colorToMove] + nonPawnTables[nonPawnHashes[getOtherColor( colorToMove )]][colorToMove][getOtherColor( colorToMove )] ) * nonpawnChWeight.value ) / 512.0;
		double major_correction = ( majorTable[majorHash][colorToMove] * majorChWeight.value ) / 512.0;
		double minor_correction = ( minorTable[minorHash][colorToMove] * minorChWeight.value ) / 512.0;
		double correction = pawn_correction + non_pawn_correction + major_correction + minor_correction;
		// lround rather than a cast: truncation biases every negative correction toward zero.
		return static_cast< int32_t >( std::lround( staticEval + ( correction / chScale.value ) ) );
	}

	inline void clear()
	{
		std::memset( pawnTable.data(), 0, sizeof( pawnTable ) );
		std::memset( nonPawnTables.data(), 0, sizeof( nonPawnTables ) );
		std::memset( majorTable.data(), 0, sizeof( majorTable ) );
		std::memset( minorTable.data(), 0, sizeof( minorTable ) );
	}

	inline void push( EColor colorToMove, uint32_t pawnHash, int32_t staticEval, const std::array<uint32_t, EColor::ColorCount>& nonPawnHashes, uint32_t majorHash, uint32_t minorHash, int32_t bestScore, int32_t depth )
	{
		int32_t error = bestScore - staticEval;
		double scaled_bonus = error * chScale.value;
		const int32_t weight = std::min( depth - 1, 16 );

		double pawnScore = pawnTable[pawnHash][colorToMove];
		pawnScore = ( pawnScore * ( chScale.value - weight ) + scaled_bonus * weight ) / chScale.value;
		pawnScore = std::clamp( pawnScore, chMin.value * chScale.value, chMax.value * chScale.value );
		pawnTable[pawnHash][colorToMove] = static_cast<int32_t>( std::lround( pawnScore ) );

		for( EColor color : { EColor::Black, EColor::White } )
		{
			double nonPawnScore = nonPawnTables[nonPawnHashes[color]][colorToMove][color];
			nonPawnScore = ( nonPawnScore * ( chScale.value - weight ) + scaled_bonus * weight ) / chScale.value;
			nonPawnScore = std::clamp( nonPawnScore, chMin.value * chScale.value, chMax.value * chScale.value );
			nonPawnTables[nonPawnHashes[color]][colorToMove][color] = static_cast<int32_t>( std::lround( nonPawnScore ) );
		}

		double majorScore = majorTable[majorHash][colorToMove];
		majorScore = ( majorScore * ( chScale.value - weight ) + scaled_bonus * weight ) / chScale.value;
		majorScore = std::clamp( majorScore, chMin.value * chScale.value, chMax.value * chScale.value );
		majorTable[majorHash][colorToMove] = static_cast<int32_t>( std::lround( majorScore ) );

		double minorScore = minorTable[minorHash][colorToMove];
		minorScore = ( minorScore * ( chScale.value - weight ) + scaled_bonus * weight ) / chScale.value;
		minorScore = std::clamp( minorScore, chMin.value * chScale.value, chMax.value * chScale.value );
		minorTable[minorHash][colorToMove] = static_cast<int32_t>( std::lround( minorScore ) );
	}
};

