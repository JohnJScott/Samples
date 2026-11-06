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
// oh boy movepicker
#pragma once

#include "globals.h"
#include "info.h"
#include "see.h"

constexpr int32_t historyCap = 16384;
constexpr int32_t killerScore = historyCap * 5 + 2;
constexpr int32_t counterScore = historyCap * 5 + 1;
constexpr int32_t goodCaptureBonus = 500000;

enum class MovegenStage 
	: int32_t
{
	TTMove = 0,
	GenNoisy,
	GoodNoisy,
	Killer,
	Counter,
	GenQuiet,
	Else,
	QSGenAll,
	QSAll,
};

inline MovegenStage& operator++( MovegenStage& v )
{
	v = static_cast< MovegenStage >( static_cast< int32_t >( v ) + 1 );
	return v;
}

class MovePicker
{
public:
	std::pair<Move, int32_t> next()
	{
		switch( stage )
		{
		case MovegenStage::TTMove:
		{
			++stage;
			if( ttMove && board.isPseudolegal( ttMove ) )
			{
				return { ttMove, 1000000000 };
			}
			[[fallthrough]];
		}
		case MovegenStage::GenNoisy:
		{
			idx = 0;
			totalMoves = board.getNoisies( moves, 0 );
			noisyEnd = totalMoves;
			scoreNoisies();

			++stage;
			[[fallthrough]];
		}
		case MovegenStage::GoodNoisy:
		{
			while( idx < totalMoves )
			{
				auto [move, score] = getNextInternal();

				if( move == ttMove )
				{
					continue;
				}

				if( move == killer )
				{
					killerGenerated = true;
				}

				if( move == counter )
				{
					counterGenerated = true;
				}

				if( score > ( goodCaptureBonus - historyCap - MVV_values[EPieceType::Queen]->value ) )
				{
					return { move, score };
				}
				else
				{
					idx--;
					break;
				}
			}

			++stage;
			[[fallthrough]];
		}
		case MovegenStage::Killer:
		{
			++stage;
			if( killer && board.isPseudolegal( killer ) && killer != ttMove && !killerGenerated )
			{
				return { killer, killerScore };
			}
			[[fallthrough]];
		}
		case MovegenStage::Counter:
		{
			++stage;
			if( counter && board.isPseudolegal( counter ) && counter != ttMove && counter != killer && !counterGenerated )
			{
				return { counter, counterScore };
			}
			[[fallthrough]];
		}
		case MovegenStage::GenQuiet:
		{
			if( !skipQuiets )
			{
				totalMoves = board.getQuiets( moves, totalMoves );
				// scores the newly generated quiets
				scoreQuiets();
			}
			++stage;

			[[fallthrough]];
		}
		case MovegenStage::Else:
		{
			while( idx < totalMoves )
			{
				auto [move, score] = getNextInternal();
				if( move == ttMove || move == killer || move == counter )
				{
					continue;
				}

				if( skipQuiets )
				{
					const uint64_t capturable = board.getOccupiedBitboard();
					bool isCapture = ( ( capturable & ( 1ULL << move.getEndSquare() ) ) != 0 ) || move.getFlag() == EnPassant;
					bool isQuiet = ( !isCapture && ( move.getFlag() <= DoublePawnPush ) );
					if( isQuiet )
					{
						continue;
					}
				}

				return { move, score };
			}

			return { Move(), 0 };
		}
		case MovegenStage::QSGenAll:
		{
			totalMoves = board.getMovesQSearch( moves );
			idx = 0;
			scoreMovesQS();

			++stage;
			[[fallthrough]];
		}
		case MovegenStage::QSAll:
		{
			return getNextInternal();
		}
		default:
			return { Move(), 0 };
		}
	}

	static MovePicker search( const Board& board, const Move ttMove, SearchInfo& tables, const int32_t ply )
	{
		return MovePicker( board, ttMove, tables, MovegenStage::TTMove, ply );
	}

	static MovePicker qsearch( const Board& board, const Move ttMove, SearchInfo& tables )
	{
		return MovePicker( board, ttMove, tables, MovegenStage::QSGenAll, 0 );
	}

	int32_t getTotalMoves() const
	{
		return totalMoves;
	}
	bool skipQuiets = false;
private:
	explicit MovePicker( const Board& board, const Move ttMove, SearchInfo& tables, const MovegenStage stage, const int32_t ply )
		: stage( stage )
		, board{ board }
		, ttMove( ttMove )
		, idx{ 0 }
		, totalMoves{ 0 }
		, info{ tables }
		, killer{ info.stack[ply].killer }
		, counter{ ply > 0 ? info.counterMoves[info.stack[ply - 1].move.getStartSquare()][info.stack[ply - 1].move.getEndSquare()] : Move() }
		, ply( ply )
	{
	}

	void scoreNoisies()
	{
		const EColor colorToMove = board.getColorToMove();
		for( int32_t i = idx; i < totalMoves; i++ )
		{
			Move move = moves[i];
			const int8_t end = move.getEndSquare();
			const int8_t start = move.getStartSquare();
			const EPieceType pieceType = getType( board.pieceAtIndex( start ) );
			// Captures!
			const EPieceType victimPieceType = getType( board.pieceAtIndex( end ) );
			// Capthist!
			moveScores[i] = static_cast<int32_t>( MVV_values[victimPieceType]->value + info.noisyHistoryTable[colorToMove][pieceType][end][victimPieceType][board.squareIsUnderAttack( end )] );
			// see!
			// try a threshold here someday
			if( see( board, move, 0 ) )
			{
				// good captures
				moveScores[i] += goodCaptureBonus;
			}
		}
	}
	void scoreQuiets()
	{
		const EColor colorToMove = board.getColorToMove();
		for( int32_t i = noisyEnd; i < totalMoves; i++ )
		{
			Move move = moves[i];
			const int8_t end = move.getEndSquare();
			const int8_t start = move.getStartSquare();
			const EPieceType pieceType = getType( board.pieceAtIndex( start ) );
			uint32_t hash = board.getPawnHashIndex();
			// read from history
			moveScores[i] = info.historyTable[colorToMove][start][board.squareIsUnderAttack( start )][end][board.squareIsUnderAttack( end )]
				+ ( ply > 0 ? ( *info.stack[ply - 1].ch_entry )[colorToMove][pieceType][end] : 0 )
				+ ( ply > 1 ? ( *info.stack[ply - 2].ch_entry )[colorToMove][pieceType][end] : 0 )
				+ ( ply > 3 ? ( *info.stack[ply - 4].ch_entry )[colorToMove][pieceType][end] : 0 )
				+ info.pawnHistoryTable[hash][colorToMove][pieceType][end];
		}
	}
	void scoreMovesQS()
	{
		const EColor colorToMove = board.getColorToMove();
		for( int32_t i = 0; i < totalMoves; i++ )
		{
			Move move = moves[i];
			const int8_t end = move.getEndSquare();
			const int8_t start = move.getStartSquare();
			const EPieceType pieceType = getType( board.pieceAtIndex( start ) );
			if( move == ttMove )
			{
				moveScores[i] = 1000000000;
			}
			else
			{
				// Captures!
				const EPieceType victimPieceType = getType( board.pieceAtIndex( end ) );
				// Capthist!
				moveScores[i] = static_cast<int32_t>( MVV_values[victimPieceType]->value + info.qsHistoryTable[colorToMove][pieceType][end][victimPieceType] );
			}
		}
	}

	std::pair<Move, int32_t> getNextInternal()
	{
		if( idx >= totalMoves )
		{
			return { Move(), 0 };
		}

		int32_t bestIdx = idx;
		for( int32_t j = idx + 1; j < totalMoves; j++ )
		{
			if( moveScores[j] > moveScores[bestIdx] )
			{
				bestIdx = j;
			}
		}

		if( bestIdx != idx )
		{
			std::swap( moveScores[bestIdx], moveScores[idx] );
			std::swap( moves[bestIdx], moves[idx] );
		}

		return { moves[idx], moveScores[idx++] };
	}
	MovegenStage stage;
	const Board& board;
	Move ttMove;
	int32_t idx;
	int32_t totalMoves;
	SearchInfo& info;
	Move killer;
	Move counter;
	bool killerGenerated = false;
	bool counterGenerated = false;
	int32_t ply;
	int32_t noisyEnd = 0;
	std::array<Move, 256> moves;
	std::array<int32_t, 256> moveScores;
};