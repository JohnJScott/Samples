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

// perft tests
struct PerftTest
{
public:
	std::string fen;
	int32_t depth;
	int32_t expectedOutput;
	PerftTest( std::string s, int32_t d, int32_t e )
	{
		fen = s;
		depth = d;
		expectedOutput = e;
	}
};

int32_t perft( Board& board, int32_t depth );
void splitPerft( Board board, int32_t depth );
void individualPerft( Board board, int32_t depth );
