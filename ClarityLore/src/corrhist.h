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

#include "globals.h"
#include "tunables.h"

struct Corrhist
{
	static constexpr uint64_t size = 16384;
	static constexpr uint64_t mask = size - 1;

	std::array<std::array<int32_t, 2>, size> pawnTable;
	std::array<std::array<std::array<int32_t, 2>, 2>, size> nonPawnTables;
	std::array<std::array<int32_t, 2>, size> majorTable;
	std::array<std::array<int32_t, 2>, size> minorTable;

	inline int32_t correct( int32_t ctm, int32_t pawnHash, int32_t staticEval, std::array<int32_t, EColor::ColorCount> nonPawnHashes, int32_t majorHash, int32_t minorHash )
	{
		int32_t pawn_correction = static_cast<int32_t>( ( pawnTable[pawnHash][ctm] * pawnChWeight.value ) / 512.0 );
		int32_t non_pawn_correction = static_cast< int32_t >( ( nonPawnTables[nonPawnHashes[ctm]][ctm][ctm] + nonPawnTables[nonPawnHashes[1 - ctm]][ctm][1 - ctm] * nonpawnChWeight.value ) / 512.0 );
		int32_t major_correction = static_cast< int32_t >( ( majorTable[majorHash][ctm] * majorChWeight.value ) / 512.0 );
		int32_t minor_correction = static_cast< int32_t >( ( minorTable[minorHash][ctm] * minorChWeight.value ) / 512.0 );
		int32_t correction = pawn_correction + non_pawn_correction + major_correction + minor_correction;
		return static_cast< int32_t >( ( staticEval + ( correction / chScale.value ) ) );
	}

	inline void clear()
	{
		std::memset( pawnTable.data(), 0, sizeof( pawnTable ) );
		std::memset( nonPawnTables.data(), 0, sizeof( nonPawnTables ) );
		std::memset( majorTable.data(), 0, sizeof( majorTable ) );
		std::memset( minorTable.data(), 0, sizeof( minorTable ) );
	}

	inline void push( int32_t pawnHash, int32_t ctm, int32_t bestScore, int32_t staticEval, int32_t depth, std::array<int32_t, EColor::ColorCount> nonPawnHashes, int32_t majorHash, int32_t minorHash )
	{
		int32_t error = bestScore - staticEval;
		int32_t scaled_bonus = static_cast< int32_t >( error * chScale.value );
		const int32_t weight = std::min( depth - 1, 16 );

		int32_t& pawnScore = pawnTable[pawnHash][ctm];
		pawnScore = static_cast< int32_t >( ( pawnScore * ( chScale.value - weight ) + scaled_bonus * weight ) / chScale.value );
		pawnScore = std::clamp( pawnScore, static_cast<int32_t>( chMin.value * chScale.value ), static_cast<int32_t>( chMax.value * chScale.value ) );

		for( int32_t color = 0; color < 2; color++ )
		{
			int32_t& nonPawnScore = nonPawnTables[nonPawnHashes[color]][ctm][color];
			nonPawnScore = static_cast< int32_t >( ( nonPawnScore * ( chScale.value - weight ) + scaled_bonus * weight ) / chScale.value );
			nonPawnScore = std::clamp( nonPawnScore, static_cast<int32_t>( chMin.value * chScale.value ), static_cast<int32_t>( chMax.value * chScale.value ) );
		}

		int32_t& majorScore = majorTable[majorHash][ctm];
		majorScore = static_cast< int32_t >( ( majorScore * ( chScale.value - weight ) + scaled_bonus * weight ) / chScale.value );
		majorScore = std::clamp( majorScore, static_cast<int32_t>( chMin.value * chScale.value ), static_cast<int32_t>( chMax.value * chScale.value ) );

		int32_t& minorScore = minorTable[minorHash][ctm];
		minorScore = static_cast< int32_t >( ( minorScore * ( chScale.value - weight ) + scaled_bonus * weight ) / chScale.value );
		minorScore = std::clamp( minorScore, static_cast<int32_t>( chMin.value * chScale.value ), static_cast<int32_t>( chMax.value * chScale.value ) );
	}
};

