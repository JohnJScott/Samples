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
#include "tt.h"
#include "info.h"
#include "limiters.h"

extern std::atomic<bool> timesUp;

constexpr int16_t matedScore = -32000;

struct Engine
{
public:
	void resetEngine();
	Move think( Board board, const SearchLimiters& limiters, bool printInfo, const std::array<Move, 256>& optimalMoves, int32_t numOptimalMoves );
	Move getBestMove() const;

	Engine( TranspositionTable* tt )
	{
		TT = tt;
	}

	uint64_t nodes = 0;
private:
	Move rootBestMove = Move();
	int32_t seldepth = 0;

	std::array<Move, 256> rootMoves;
	int32_t numRootMoves;

	TranspositionTable* TT;
	SearchInfo info;
	SearchLimiters limits;
	std::array<std::array<int32_t, 64>, 64> nodeTMTable;
	std::chrono::steady_clock::time_point begin;

	void clearHistory();
	int16_t qSearch( Board& board, int32_t alpha, int32_t beta, int16_t ply );
	void updateHistory( const int32_t colorToMove, const int32_t start, const int32_t end, const int32_t piece, const int32_t bonus, const int16_t ply, const int32_t hash, const bool startAttack, const bool endAttack );
	void updateNoisyHistory( const int32_t colorToMove, const int32_t piece, const int32_t end, const int32_t victim, const int32_t bonus, const bool endAttack );
	void updateQSHistory( const int32_t colorToMove, const int32_t piece, const int32_t end, const int32_t victim, const int32_t bonus );
	int16_t negamax( Board& board, int32_t depth, int32_t alpha, int32_t beta, int16_t ply, bool nmpAllowed, bool isCutNode );
	std::string getPV();
	void outputInfo( const Board& board, int32_t score, int32_t depth, int32_t elapsedTime );
};
