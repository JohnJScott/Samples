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

inline int estimateMoveValue( const Board& board, const int8_t end, const int32_t flag )
{
	// starting with the end square piece
	int32_t value = static_cast<int32_t>( SEE_values[getType( board.pieceAtIndex( end ) )]->value );
	// promotions! pawn--, newpiece++
	// promotions is indexed by EPieceType, so only Knight..Queen are promotion flags
	for( int32_t promoted = EPieceType::Knight; promoted <= EPieceType::Queen; promoted++ )
	{
		if( flag == promotions[promoted] )
		{
			value = static_cast<int32_t>( SEE_values[promoted]->value - SEE_values[EPieceType::Pawn]->value );
			return value;
		}
	}

	// Target square is empty for en passant, but you still capture a pawn
	if( flag == EnPassant )
	{
		value = static_cast<int32_t>( SEE_values[EPieceType::Pawn]->value );
	}
	// castling can't capture and is never encoded as such so we don't care.
	return value;
}

inline bool see( const Board& board, Move move, int32_t threshold )
{
	// establishing stuff
	const int8_t start = move.getStartSquare();
	const int8_t end = move.getEndSquare();
	const uint16_t flag = move.getFlag();

	EPieceType nextVictim = getType( board.pieceAtIndex( start ) );
	// handle promotions
	// promotion flags are the 4 highest numbers, so this saves a loop if it's not necessary
	if( flag > DoublePawnPush )
	{
		for( EPieceType promoted : { EPieceType::Knight, EPieceType::Bishop, EPieceType::Rook, EPieceType::Queen } )
		{
			if( flag == promotions[promoted] )
			{
				nextVictim = promoted;
				break;
			}
		}
	}

	int32_t balance = estimateMoveValue( board, end, flag ) - threshold;
	// best case still doesn't beat threshold, not good
	if( balance < 0 )
	{
		return false;
	}

	// worst case, we lose the piece here
	balance -= static_cast<int32_t>( SEE_values[nextVictim]->value );
	// if it's still winning in the best case scenario, we can just cut it off
	if( balance >= 0 )
	{
		return true;
	}

	// make sure occupied bitboard knows we did the first move
	uint64_t occupied = board.getOccupiedBitboard();
	occupied = ( occupied ^ ( 1ULL << start ) ) | ( 1ULL << end );
	if( flag == EnPassant )
	{
		occupied ^= ( 1ULL << board.getEnPassantIndex() );
	}

	EColor color = board.getOtherColorToMove();
	// get the pieces, for detecting revealed attackers
	const uint64_t bishops = board.getPieceBitboard( EPieceType::Bishop ) | board.getPieceBitboard( EPieceType::Queen );
	const uint64_t rooks = board.getPieceBitboard( EPieceType::Rook ) | board.getPieceBitboard( EPieceType::Queen );
	// generate the attackers (not including the first move)
	uint64_t attackers = board.getAttackers( end ) & occupied;
	while( true )
	{
		// get the attackers
		uint64_t myAttackers = attackers & board.getColoredBitboard( color );
		// if no attackers, you're done
		if( myAttackers == 0ULL )
		{
			break;
		}

		// find lowest value attacker
		for( EPieceType nextVictimCheck : { EPieceType::Pawn, EPieceType::Knight, EPieceType::Bishop, EPieceType::Rook, EPieceType::Queen, EPieceType::King } )
		{
			if( ( myAttackers & board.getColoredPieceBitboard( color, nextVictimCheck ) ) != 0 )
			{
				nextVictim = nextVictimCheck;
				break;
			}
		}

		// FIXME: if no new victim found, assume king?

		// make the move
		occupied ^= ( 1ULL << std::countr_zero( myAttackers & board.getColoredPieceBitboard( color, nextVictim ) ) );
		// diagonal moves may reveal more attackers
		if( nextVictim == EPieceType::Pawn || nextVictim == EPieceType::Bishop || nextVictim == EPieceType::Queen )
		{
			attackers |= ( getBishopAttacks( end, occupied ) & bishops );
		}

		// orthogonal moves may reveal more attackers
		if( nextVictim == EPieceType::Rook || nextVictim == EPieceType::Queen )
		{
			attackers |= ( getRookAttacks( end, occupied ) & rooks );
		}

		attackers &= occupied;
		color = getOtherColor( color );
		// update balance
		balance = static_cast<int32_t>( -balance - 1 - SEE_values[nextVictim]->value );
		// if you are ahead
		if( balance >= 0 )
		{
			// speedrunning legality check
			if( nextVictim == EPieceType::King && ( ( attackers & board.getColoredBitboard( color ) ) != 0 ) )
			{
				color = getOtherColor( color );
			}
			break;
		}
	}

	// if color is different, than you either ran out of attackers and lost the exchange or the opponent won
	return board.getColorToMove() != color;
}