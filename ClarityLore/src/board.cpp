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
#include <map>

#include "globals.h"
#include "corrhist.h"

template bool Board::makeMove<false>( Move move );
template void Board::undoMove<false>();
template bool Board::makeMove<true>( Move move );
template void Board::undoMove<true>();
// node counting only - no accumulator work at all
template bool Board::makeMove<false, false>( Move move );
template void Board::undoMove<false, false>();
template void Board::addPiece<false>( int8_t square, EPiece piece );
template void Board::removePiece<false>( int8_t square, EPiece piece );
template void Board::movePiece<false>( int8_t square1, EPiece piece1, int8_t square2, EPiece piece2 );
template void Board::addPiece<true>( int8_t square, EPiece piece );
template void Board::removePiece<true>( int8_t square, EPiece piece );
template void Board::movePiece<true>( int8_t square1, EPiece piece1, int8_t square2, EPiece piece2 );

// zobrist hashing values 
std::array<std::array<uint64_t, EPiece::PieceCount>, numBoardSquares> zobTable;
// if black is to move this value is xor'ed
uint64_t zobColorToMove;

// masks for castling rights, used to update the castling rights faster after a rook or king move
static constexpr std::array<uint8_t, numBoardSquares> rookRightMasks =
{
	0b1101,255,255,255,255,255,255,0b1110,
	255,   255,255,255,255,255,255,   255,
	255,   255,255,255,255,255,255,   255,
	255,   255,255,255,255,255,255,   255,
	255,   255,255,255,255,255,255,   255,
	255,   255,255,255,255,255,255,   255,
	255,   255,255,255,255,255,255,   255,
	0b0111,255,255,255,255,255,255,0b1011
};

static constexpr std::array<uint8_t, EColor::ColorCount> kingRightMasks =
{
	0b0011, 
	0b1100
};

static constexpr std::array<int8_t, 8> directionalOffsets =
{
	8, -8, 1, -1, 7, -7, 9, -9
};

static constexpr std::array<char, EPiece::PieceCount> pieceToChar =
{
	' ', 'p', 'n', 'b', 'r', 'q', 'k', ' ',
	' ', 'P', 'N', 'B', 'R', 'Q', 'K'
};

static constexpr std::array<const char*, EPiece::PieceCount> pieceToPieceChar =
{
	"\xC2\xB7", "\xE2\x99\x9F", "\xE2\x99\x9E", "\xE2\x99\x9D", "\xE2\x99\x9C", "\xE2\x99\x9B", "\xE2\x99\x9A", "\xC2\xB7",
	"\xC2\xB7", "\xE2\x99\x99", "\xE2\x99\x98", "\xE2\x99\x97", "\xE2\x99\x96", "\xE2\x99\x95", "\xE2\x99\x94"
};

static const std::map<char, EPiece>& charToEnum()
{
	static const std::map<char, EPiece> table =
	{
		{ 'p', EPiece::BlackPawn },
		{ 'P', EPiece::WhitePawn },
		{ 'n', EPiece::BlackKnight },
		{ 'N', EPiece::WhiteKnight },
		{ 'b', EPiece::BlackBishop },
		{ 'B', EPiece::WhiteBishop },
		{ 'r', EPiece::BlackRook },
		{ 'R', EPiece::WhiteRook },
		{ 'q', EPiece::BlackQueen },
		{ 'Q', EPiece::WhiteQueen },
		{ 'k', EPiece::BlackKing },
		{ 'K', EPiece::WhiteKing }
	};

	return table;
}

void Board::displayLetters() const
{
	for( int8_t rank = numRanks; rank > 0; rank-- )
	{
		std::cout << std::to_string( rank ) << " ";
		for( int8_t file = 0; file < numFiles; file++ )
		{
			EPiece piece = pieceAtIndex( ( ( rank - 1 ) << 3 ) | file );
			std::cout << pieceToChar[piece] << " ";
		}

		std::cout << '\n';
	}

	std::cout << "  a b c d e f g h\n\n";
}

void Board::displayPieces() const
{
	for( int8_t rank = numRanks; rank > 0; rank-- )
	{
		std::cout << std::to_string( rank ) << " ";
		for( int8_t file = 0; file < numFiles; file++ )
		{
			EPiece piece = pieceAtIndex( ( ( rank - 1 ) << 3 ) | file );
			std::cout << pieceToPieceChar[piece] << " ";
		}

		std::cout << "\n";
	}

	std::cout << "  a b c d e f g h\n\n";
}

void Board::toString() const
{
	displayLetters();
	displayPieces();

	std::cout << "Ply count: " << std::to_string( plyCount ) << '\n';
	std::cout << "Hash: " << std::to_string( stateHistory.back().zobristHash ) << '\n';
	std::cout << "Castling rights: " << std::to_string( stateHistory.back().castlingRights & 1 ) 
									<< std::to_string( ( stateHistory.back().castlingRights >> 1 ) & 1 ) 
									<< std::to_string( ( stateHistory.back().castlingRights >> 2 ) & 1 ) 
									<< std::to_string( ( stateHistory.back().castlingRights >> 3 ) & 1 ) << '\n';

	if( stateHistory.back().enPassantIndex == 64 )
	{
		std::cout << "En passant square: none\n";
	}
	else
	{
		std::cout << "En passant square: " << squareNames[stateHistory.back().enPassantIndex] << '\n';
	}

	std::cout << "Color to move: " << ( colorToMove == EColor::Black ? "black" : "white" ) << '\n';
	std::cout << "Evaluation: " << std::to_string( getEvaluation() ) << '\n';
}

Board::Board( const std::string& fen )
{
	stateHistory.clear();
	stateHistory.reserve( 256 );
	stateHistory.emplace_back();
	nnueState.reset();
	stateHistory.back().zobristHash = 0;
	stateHistory.back().pawnHash = 0;
	stateHistory.back().nonPawnHashes[EColor::Black] = 0;
	stateHistory.back().nonPawnHashes[EColor::White] = 0;
	stateHistory.back().majorHash = 0;
	stateHistory.back().minorHash = 0;

	for( int32_t i = 0; i < 64; i++ )
	{
		stateHistory.back().mailbox[i] = EPiece::None;
	}

	for( int32_t i = 0; i < 6; i++ )
	{
		stateHistory.back().pieceBitboards[i] = 0ULL;
	}

	for( int32_t i = 0; i < 2; i++ )
	{
		stateHistory.back().coloredBitboards[i] = 0ULL;
	}

	// main board state, segment 1
	std::vector<std::string> segments = split( fen, ' ' );
	std::vector<std::string> ranks = split( segments[0], '/' );
	std::ranges::reverse( ranks );
	int8_t square = 0;
	for( const std::string& rank : ranks )
	{
		for( char c : rank )
		{
			const std::map<char, EPiece>& pieceCharacters = charToEnum();
			const std::map<char, EPiece>::const_iterator entry = pieceCharacters.find( c );
			if( entry != pieceCharacters.end() )
			{
				EPiece piece = entry->second;
				addPiece<false>( square, piece );

				if( piece == EPiece::BlackKing )
				{
					stateHistory.back().kingSquares[EColor::Black] = square;
				}
				else if( piece == EPiece::WhiteKing )
				{
					stateHistory.back().kingSquares[EColor::White] = square;
				}

				square++;
			}
			else
			{
				square += c - '0';
			}
		}
	}

	// convert color to move into 0 or 1, segment 2
	colorToMove = ( segments[1] == "w" ? EColor::White : EColor::Black );
	if( colorToMove == EColor::White )
	{
		stateHistory.back().zobristHash ^= zobColorToMove;
	}

	// decode the castling rights into the 4 bit number, segment 3
	stateHistory.back().castlingRights = 0;
	for( char c : segments[2] )
	{
		if( c == 'K' )
		{
			stateHistory.back().castlingRights |= 1;
		}
		else if( c == 'Q' )
		{
			stateHistory.back().castlingRights |= 2;
		}
		else if( c == 'k' )
		{
			stateHistory.back().castlingRights |= 4;
		}
		else if( c == 'q' )
		{
			stateHistory.back().castlingRights |= 8;
		}
	}

	// decode the en passant index, segment 4
	if( segments[3] != "-" )
	{
		square = 0;
		uint8_t num = 0;
		for( char c : segments[3] )
		{
			if( square == 0 )
			{
				num += ( c - 'a' );
			}
			else
			{
				num += ( c - '1' ) << 3;
			}

			square++;
		}

		stateHistory.back().enPassantIndex = num;
	}
	else
	{
		stateHistory.back().enPassantIndex = 64;
	}

	// 50 move counter, segment 5
	stateHistory.back().hundredPlyCounter = static_cast<uint8_t>( std::stoi( segments[4] ) );
	// ply count, segment 6
	plyCount = ( std::stoi( segments[5] ) << 1 ) - colorToMove;
	nnueState.refreshAccumulator( EColor::Black, stateHistory.back(), stateHistory.back().kingSquares[EColor::Black] );
	nnueState.refreshAccumulator( EColor::White, stateHistory.back(), stateHistory.back().kingSquares[EColor::White] );
	stateHistory.back().threats = calculateThreats();
}

std::string Board::getFenString() const
{
	// essentially copy and pasted from my c# engine lol
	std::string fen;
	for( int8_t rank = numRanks; rank > 0; rank-- )
	{
		if( rank != numRanks )
		{
			fen += '/';
		}

		int8_t numEmptyFiles = 0;
		for( int8_t file = 0; file < numFiles; file++ )
		{
			EPiece piece = pieceAtIndex( ( ( rank - 1 ) << 3 ) | file );
			if( piece != EPiece::None )
			{
				if( numEmptyFiles != 0 )
				{
					fen += std::to_string( numEmptyFiles );
					numEmptyFiles = 0;
				}

				fen += pieceToChar[piece];
			}
			else
			{
				numEmptyFiles++;
			}
		}

		if( numEmptyFiles != 0 )
		{
			fen += std::to_string( numEmptyFiles );
		}
	}

	// color to move
	fen += ' ';
	fen += ( colorToMove == EColor::Black ? 'b' : 'w' );
	// castling rights
	fen += ' ';
	bool thingAdded = false;
	if( ( stateHistory.back().castlingRights & 1 ) != 0 )
	{
		fen += 'K';
		thingAdded = true;
	}

	if( ( stateHistory.back().castlingRights & 2 ) != 0 )
	{
		fen += 'Q';
		thingAdded = true;
	}

	if( ( stateHistory.back().castlingRights & 4 ) != 0 )
	{
		fen += 'k';
		thingAdded = true;
	}

	if( ( stateHistory.back().castlingRights & 8 ) != 0 )
	{
		fen += 'q';
		thingAdded = true;
	}

	if( thingAdded == false )
	{
		fen += '-';
	}

	// en passant square
	fen += ' ';
	if( stateHistory.back().enPassantIndex == 64 )
	{
		fen += '-';
	}
	else
	{
		fen += squareNames[stateHistory.back().enPassantIndex];
	}

	// 50 move counter
	fen += ' ';
	// the FEN halfmove clock is already counted in plies, so no conversion here - unlike the
	// fullmove number below, which does divide plyCount down to moves.
	fen += std::to_string( stateHistory.back().hundredPlyCounter );
	// ply count
	fen += ' ';
	fen += std::to_string( ( plyCount >> 1 ) + colorToMove );
	return fen;
}

template <bool UpdateNNUE> void Board::addPiece( int8_t square, EPiece piece )
{
	assert( piece != EPiece::None );
	assert( piece < 16 );
	assert( square < 64 );
	assert( pieceAtIndex( square ) == EPiece::None );
	assert( square >= 0 );

	const uint64_t bitboardSquare = squareToBitboard[square];
	//std::cout << "Adding piece of type " << std::to_string(type) << " at index " << std::to_string(square) << '\n';
	stateHistory.back().coloredBitboards[getColor( piece )] ^= bitboardSquare;
	stateHistory.back().pieceBitboards[getType( piece )] ^= bitboardSquare;
	stateHistory.back().mailbox[square] = piece;
	assert( pieceAtIndex( square ) == piece );
	if constexpr( UpdateNNUE )
	{
		nnueState.activateFeature( square, piece, stateHistory.back().kingSquares[EColor::Black], stateHistory.back().kingSquares[EColor::White] );
	}

	stateHistory.back().zobristHash ^= zobTable[square][piece];
	if( getType( piece ) == EPieceType::Pawn )
	{
		stateHistory.back().pawnHash ^= zobTable[square][piece];
	}
	else
	{
		stateHistory.back().nonPawnHashes[getColor( piece )] ^= zobTable[square][piece];
		if( getType( piece ) == EPieceType::Queen || getType( piece ) == EPieceType::Rook )
		{
			stateHistory.back().majorHash ^= zobTable[square][piece];
		}
		else if( getType( piece ) != EPieceType::King )
		{
			stateHistory.back().minorHash ^= zobTable[square][piece];
		}
	}
}

template <bool UpdateNNUE> void Board::removePiece( int8_t square, EPiece piece )
{
	assert( piece != EPiece::None );
	assert( piece < 16 );
	assert( square < 64 );
	assert( pieceAtIndex( square ) == piece );
	assert( square >= 0 );

	const uint64_t bitboardSquare = squareToBitboard[square];
	stateHistory.back().coloredBitboards[getColor( piece )] ^= bitboardSquare;
	stateHistory.back().pieceBitboards[getType( piece )] ^= bitboardSquare;
	stateHistory.back().mailbox[square] = EPiece::None;
	if constexpr( UpdateNNUE )
	{
		nnueState.disableFeature( square, piece, stateHistory.back().kingSquares[EColor::Black], stateHistory.back().kingSquares[EColor::White] );
	}

	stateHistory.back().zobristHash ^= zobTable[square][piece];
	if( getType( piece ) == EPieceType::Pawn )
	{
		stateHistory.back().pawnHash ^= zobTable[square][piece];
	}
	else
	{
		stateHistory.back().nonPawnHashes[getColor( piece )] ^= zobTable[square][piece];
		if( getType( piece ) == EPieceType::Queen || getType( piece ) == EPieceType::Rook )
		{
			stateHistory.back().majorHash ^= zobTable[square][piece];
		}
		else if( getType( piece ) != EPieceType::King )
		{
			stateHistory.back().minorHash ^= zobTable[square][piece];
		}
	}
	assert( pieceAtIndex( square ) == EPiece::None );
}

template <bool UpdateNNUE> void Board::movePiece( int8_t square1, EPiece piece1, int8_t square2, EPiece piece2 )
{
	assert( piece1 != EPiece::None );
	assert( square1 < 64 );
	assert( square2 < 64 );
	if( piece2 != EPiece::None )
	{
		removePiece<UpdateNNUE>( square2, piece2 );
	}

	addPiece<UpdateNNUE>( square2, piece1 );
	removePiece<UpdateNNUE>( square1, piece1 );
}

uint64_t Board::getColoredPieceBitboard( EColor color, EPieceType pieceType ) const
{
	return stateHistory.back().pieceBitboards[pieceType] & stateHistory.back().coloredBitboards[color];
}

uint64_t Board::getOccupiedBitboard() const
{
	return stateHistory.back().coloredBitboards[0] | stateHistory.back().coloredBitboards[1];
}

// fills up the array and then returns the number of moves to be looped through later.
int32_t Board::getMoves( std::array<Move, 256>& moves ) const
{
	uint64_t occupiedBitboard = getOccupiedBitboard();
	int32_t totalMoves = 0;

	// castling
	if( ( stateHistory.back().castlingRights & kingRightMasks[1 - colorToMove] ) != 0 )
	{
		if( !isInCheck() )
		{
			if( colorToMove == EColor::White )
			{
				if( ( stateHistory.back().castlingRights & 1 ) != 0 && ( occupiedBitboard & 0x60 ) == 0 && !squareIsUnderAttack( 5 ) )
				{
					moves[totalMoves] = Move( 4, 6, castling[0] );
					totalMoves++;
				}
				if( ( stateHistory.back().castlingRights & 2 ) != 0 && ( occupiedBitboard & 0xE ) == 0 && !squareIsUnderAttack( 3 ) )
				{
					moves[totalMoves] = Move( 4, 2, castling[1] );
					totalMoves++;
				}
			}
			else
			{
				if( ( stateHistory.back().castlingRights & 4 ) != 0 && ( occupiedBitboard & 0x6000000000000000 ) == 0 && !squareIsUnderAttack( 61 ) )
				{
					moves[totalMoves] = Move( 60, 62, castling[2] );
					totalMoves++;
				}
				if( ( stateHistory.back().castlingRights & 8 ) != 0 && ( occupiedBitboard & 0xE00000000000000 ) == 0 && !squareIsUnderAttack( 59 ) )
				{
					moves[totalMoves] = Move( 60, 58, castling[3] );
					totalMoves++;
				}
			}
		}
	}

	uint64_t mask = stateHistory.back().coloredBitboards[colorToMove] ^ getColoredPieceBitboard( colorToMove, EPieceType::Pawn );
	// the rest of the pieces
	while( mask != 0 )
	{
		int8_t startSquare = popLSB( mask );
		EPieceType currentType = getType( pieceAtIndex( startSquare ) );
		uint64_t total = 0;
		if( currentType == EPieceType::Knight )
		{
			total = getKnightAttacks( startSquare );
		}
		else if( currentType == EPieceType::Bishop )
		{
			total = getBishopAttacks( startSquare, occupiedBitboard );
		}
		else if( currentType == EPieceType::Rook )
		{
			total = getRookAttacks( startSquare, occupiedBitboard );
		}
		else if( currentType == EPieceType::Queen )
		{
			total = getRookAttacks( startSquare, occupiedBitboard ) | getBishopAttacks( startSquare, occupiedBitboard );
		}
		else if( currentType == EPieceType::King )
		{
			total = getKingAttacks( startSquare );
		}

		// get rid of capturing your own pieces
		total ^= ( total & stateHistory.back().coloredBitboards[colorToMove] );
		while( total != 0 )
		{
			moves[totalMoves] = Move( startSquare, popLSB( total ), Normal );
			totalMoves++;
		}
	}

	// pawn pushes
	uint64_t pawnBitboard = getColoredPieceBitboard( colorToMove, EPieceType::Pawn );
	uint64_t emptyBitboard = ~occupiedBitboard;
	uint64_t pawnPushes = getPawnPushes( pawnBitboard, emptyBitboard, colorToMove );
	uint64_t doublePawnPushes = getDoublePawnPushes( pawnPushes, emptyBitboard, colorToMove );
	uint64_t pawnPushPromotions = pawnPushes & getRankMask( 7 * colorToMove );
	pawnPushes ^= pawnPushPromotions;
	while( pawnPushes != 0 )
	{
		int8_t index = popLSB( pawnPushes );
		int8_t startSquare = index + directionalOffsets[colorToMove];
		moves[totalMoves] = Move( startSquare, index, Normal );
		totalMoves++;
	}

	while( doublePawnPushes != 0 )
	{
		int8_t index = popLSB( doublePawnPushes );
		int8_t startSquare = index + ( directionalOffsets[colorToMove] << 1 );
		assert( getType( pieceAtIndex( startSquare ) ) == EPieceType::Pawn );
		moves[totalMoves] = Move( startSquare, index, DoublePawnPush );
		totalMoves++;
	}

	while( pawnPushPromotions != 0 )
	{
		int8_t index = popLSB( pawnPushPromotions );
		int8_t startSquare = index + directionalOffsets[colorToMove];
		for( int32_t piece = EPieceType::Knight; piece < EPieceType::King; piece++ )
		{
			moves[totalMoves] = Move( startSquare, index, promotions[piece] );
			totalMoves++;
		}
	}

	// pawn captures
	uint64_t capturable = stateHistory.back().coloredBitboards[getOtherColorToMove()];
	if( stateHistory.back().enPassantIndex != 64 )
	{
		capturable |= squareToBitboard[stateHistory.back().enPassantIndex];
	}

	uint64_t leftCaptures = ( colorToMove == EColor::Black ? pawnBitboard >> 9 : pawnBitboard << 7 );
	leftCaptures &= ~getFileMask( 7 );
	leftCaptures &= capturable;
	uint64_t leftCapturePromotions = leftCaptures & getRankMask( 7 * colorToMove );
	leftCaptures ^= leftCapturePromotions;

	uint64_t rightCaptures = ( colorToMove == EColor::Black ? pawnBitboard >> 7 : pawnBitboard << 9 );
	rightCaptures &= ~getFileMask( 0 );
	rightCaptures &= capturable;
	uint64_t rightCapturePromotions = rightCaptures & getRankMask( 7 * colorToMove );
	rightCaptures ^= rightCapturePromotions;

	while( leftCaptures != 0 )
	{
		int8_t index = popLSB( leftCaptures );
		int8_t startSquare = index + ( colorToMove == EColor::Black ? 9 : -7 );
		moves[totalMoves] = Move( startSquare, index, ( index == stateHistory.back().enPassantIndex ? EnPassant : Normal ) );
		totalMoves++;
	}

	while( rightCaptures != 0 )
	{
		int8_t index = popLSB( rightCaptures );
		int8_t startSquare = index + ( colorToMove == EColor::Black ? 7 : -9 );
		moves[totalMoves] = Move( startSquare, index, ( index == stateHistory.back().enPassantIndex ? EnPassant : Normal ) );
		totalMoves++;
	}

	while( leftCapturePromotions != 0 )
	{
		int8_t index = popLSB( leftCapturePromotions );
		int8_t startSquare = index + ( colorToMove == EColor::Black ? 9 : -7 );
		for( int32_t piece = EPieceType::Knight; piece < EPieceType::King; piece++ )
		{
			moves[totalMoves] = Move( startSquare, index, promotions[piece] );
			totalMoves++;
		}
	}

	while( rightCapturePromotions != 0 )
	{
		int8_t index = popLSB( rightCapturePromotions );
		int8_t startSquare = index + ( colorToMove == EColor::Black ? 7 : -9 );
		for( int32_t piece = EPieceType::Knight; piece < EPieceType::King; piece++ )
		{
			moves[totalMoves] = Move( startSquare, index, promotions[piece] );
			totalMoves++;
		}
	}

	return totalMoves;
}

int32_t Board::getMovesQSearch( std::array<Move, 256>& moves ) const
{
	const uint64_t occupiedBitboard = getOccupiedBitboard();
	int32_t totalMoves = 0;
	uint64_t mask = stateHistory.back().coloredBitboards[colorToMove] ^ getColoredPieceBitboard( colorToMove, EPieceType::Pawn );
	// the rest of the pieces
	while( mask != 0 )
	{
		const int8_t startSquare = popLSB( mask );
		const EPieceType pieceType = getType( pieceAtIndex( startSquare ) );
		uint64_t total = 0;
		if( pieceType == EPieceType::Knight )
		{
			total = getKnightAttacks( startSquare );
		}
		else if( pieceType == EPieceType::Bishop )
		{
			total = getBishopAttacks( startSquare, occupiedBitboard );
		}
		else if( pieceType == EPieceType::Rook )
		{
			total = getRookAttacks( startSquare, occupiedBitboard );
		}
		else if( pieceType == EPieceType::Queen )
		{
			total = getRookAttacks( startSquare, occupiedBitboard ) | getBishopAttacks( startSquare, occupiedBitboard );
		}
		else if( pieceType == EPieceType::King )
		{
			total = getKingAttacks( startSquare );
		}

		// Only captures or (not done yet) checks
		total &= stateHistory.back().coloredBitboards[getOtherColorToMove()];
		while( total != 0 )
		{
			moves[totalMoves] = Move( startSquare, popLSB( total ), Normal );
			totalMoves++;
		}
	}
	// pawn push promotions
	const uint64_t pawnBitboard = getColoredPieceBitboard( colorToMove, EPieceType::Pawn );
	const uint64_t emptyBitboard = ~occupiedBitboard;
	const uint64_t pawnPushes = getPawnPushes( pawnBitboard, emptyBitboard, colorToMove );
	uint64_t pawnPushPromotions = pawnPushes & getRankMask( 7 * colorToMove );
	while( pawnPushPromotions != 0 )
	{
		const int8_t index = popLSB( pawnPushPromotions );
		const int8_t startSquare = static_cast< uint8_t >( ( index + directionalOffsets[colorToMove] ) );
		for( int32_t piece = EPieceType::Knight; piece < EPieceType::King; piece++ )
		{
			moves[totalMoves] = Move( startSquare, index, promotions[piece] );
			totalMoves++;
		}
	}

	// captures
	uint64_t capturable = stateHistory.back().coloredBitboards[1 - colorToMove];
	if( stateHistory.back().enPassantIndex != 64 )
	{
		capturable |= squareToBitboard[stateHistory.back().enPassantIndex];
	}

	uint64_t leftCaptures = ( colorToMove == EColor::Black ? pawnBitboard >> 9 : pawnBitboard << 7 );
	leftCaptures &= ~getFileMask( 7 );
	leftCaptures &= capturable;
	uint64_t leftCapturePromotions = leftCaptures & getRankMask( 7 * colorToMove );
	leftCaptures ^= leftCapturePromotions;

	uint64_t rightCaptures = ( colorToMove == EColor::Black ? pawnBitboard >> 7 : pawnBitboard << 9 );
	rightCaptures &= ~getFileMask( 0 );
	rightCaptures &= capturable;
	uint64_t rightCapturePromotions = rightCaptures & getRankMask( 7 * colorToMove );
	rightCaptures ^= rightCapturePromotions;

	while( leftCaptures != 0 )
	{
		int8_t index = popLSB( leftCaptures );
		int8_t startSquare = index + ( colorToMove == EColor::Black ? 9 : -7 );
		moves[totalMoves] = Move( startSquare, index, ( index == stateHistory.back().enPassantIndex ? EnPassant : Normal ) );
		totalMoves++;
	}

	while( rightCaptures != 0 )
	{
		int8_t index = popLSB( rightCaptures );
		int8_t startSquare = index + ( colorToMove == EColor::Black ? 7 : -9 );
		moves[totalMoves] = Move( startSquare, index, ( index == stateHistory.back().enPassantIndex ? EnPassant : Normal ) );
		totalMoves++;
	}

	while( leftCapturePromotions != 0 )
	{
		int8_t index = popLSB( leftCapturePromotions );
		int8_t startSquare = index + ( colorToMove == EColor::Black ? 9 : -7 );
		for( int32_t piece = EPieceType::Knight; piece < EPieceType::King; piece++ )
		{
			moves[totalMoves] = Move( startSquare, index, promotions[piece] );
			totalMoves++;
		}
	}

	while( rightCapturePromotions != 0 )
	{
		int8_t index = popLSB( rightCapturePromotions );
		int8_t startSquare = index + ( colorToMove == EColor::Black ? 7 : -9 );
		for( int32_t piece = EPieceType::Knight; piece < EPieceType::King; piece++ )
		{
			moves[totalMoves] = Move( startSquare, index, promotions[piece] );
			totalMoves++;
		}
	}

	return totalMoves;
}

bool Board::isInCheck() const
{
	return squareIsUnderAttack( stateHistory.back().kingSquares[colorToMove] );
}

// thanks ciekce, shoutout stormphrax
bool Board::squareIsUnderAttack( int32_t square ) const
{
	if( stateHistory.back().threats != 0 )
	{
		return ( stateHistory.back().threats & ( 1ULL << square ) );
	}
	else
	{
		const EColor opponent = getOtherColorToMove();
		const uint64_t queens = getColoredPieceBitboard( opponent, EPieceType::Queen );

		uint64_t mask = getRookAttacks( square, getOccupiedBitboard() ) & ( queens | getColoredPieceBitboard( opponent, EPieceType::Rook ) );
		if( mask != 0 )
		{
			return true;
		}

		mask = getBishopAttacks( square, getOccupiedBitboard() ) & ( queens | getColoredPieceBitboard( opponent, EPieceType::Bishop ) );
		if( mask != 0 )
		{
			return true;
		}

		mask = getKnightAttacks( square ) & getColoredPieceBitboard( opponent, EPieceType::Knight );
		if( mask != 0 )
		{
			return true;
		}

		mask = getPawnAttacks( square, colorToMove ) & getColoredPieceBitboard( opponent, EPieceType::Pawn );
		if( mask != 0 )
		{
			return true;
		}

		mask = getKingAttacks( square ) & getColoredPieceBitboard( opponent, EPieceType::King );
		if( mask != 0 )
		{
			return true;
		}

		return false;
	}
}

template <bool PushNNUE, bool UpdateNNUE> bool Board::makeMove( Move move )
{
	//std::cout << "move " << toLongAlgebraic(move) << " on position " << getFenString() << std::endl;
	//std::cout << "makemove " << toLongAlgebraic(move) << std::endl;
	// push to vectors
	stateHistory.push_back( stateHistory.back() );
	NetworkUpdates updates;
	stateHistory.back().threats = 0;

	// get information
	int8_t start = move.getStartSquare();
	int8_t end = move.getEndSquare();
	EPiece movedPiece = pieceAtIndex( move.getStartSquare() );
	EPiece victim = pieceAtIndex( move.getEndSquare() );
	int32_t flag = move.getFlag();
	assert( movedPiece != EPiece::None );
	bool isCapture = ( victim != EPiece::None );
	EPieceType movedPieceType = getType( movedPiece );

	// hundred Ply (fifty move) counter
	stateHistory.back().hundredPlyCounter++;
	if( isCapture || movedPieceType == EPieceType::Pawn )
	{
		stateHistory.back().hundredPlyCounter = 0;
	}

	// king square updates
	if( movedPieceType == EPieceType::King )
	{
		if( refreshRequired( colorToMove, start, end ) )
		{
			updates.pushBucket( end, colorToMove );
		}

		stateHistory.back().kingSquares[colorToMove] = static_cast<uint8_t>( end );
	}

	// actually make the move
	if( isCapture )
	{
		removePiece<false>( end, victim );
		updates.pushSub( end, victim );
	}

	removePiece<false>( start, movedPiece );
	updates.pushSub( start, movedPiece );
	if( flag < promotions[EPieceType::Knight] )
	{
		addPiece<false>( end, movedPiece );
		updates.pushAdd( end, movedPiece );
	}

	// En Passant
	stateHistory.back().enPassantIndex = 64;

	// castling rights updates!
	if( ( stateHistory.back().castlingRights & kingRightMasks[getOtherColorToMove()] ) != 0 )
	{
		switch( movedPieceType )
		{
		case EPieceType::Rook:
			stateHistory.back().castlingRights &= rookRightMasks[start];
			break;
		case EPieceType::King:
			stateHistory.back().castlingRights &= kingRightMasks[colorToMove];
			break;
		default:
			break;
		}
	}

	if( getType( victim ) == EPieceType::Rook )
	{
		switch( end )
		{
		case 7:
			stateHistory.back().castlingRights &= rookRightMasks[end];
			break;
		case 0:
			stateHistory.back().castlingRights &= rookRightMasks[end];
			break;
		case 63:
			stateHistory.back().castlingRights &= rookRightMasks[end];
			break;
		case 56:
			stateHistory.back().castlingRights &= rookRightMasks[end];
			break;
		default:
			break;
		}
	}

	// edge cases!
	switch( flag )
	{
		// castling cases
	case castling[0]:
		assert( pieceAtIndex( 7 ) != EPiece::None );
		movePiece<false>( 7, EPiece::WhiteRook, 5, None );
		updates.pushSub( 7, EPiece::WhiteRook );
		updates.pushAdd( 5, EPiece::WhiteRook );
		break;

	case castling[1]:
		assert( pieceAtIndex( 0 ) != EPiece::None );
		movePiece<false>( 0, EPiece::WhiteRook, 3, None );
		updates.pushSub( 0, EPiece::WhiteRook );
		updates.pushAdd( 3, EPiece::WhiteRook );
		break;

	case castling[2]:
		assert( pieceAtIndex( 63 ) != EPiece::None );
		movePiece<false>( 63, EPiece::BlackRook, 61, None );
		updates.pushSub( 63, EPiece::BlackRook );
		updates.pushAdd( 61, EPiece::BlackRook );
		break;

	case castling[3]:
		assert( pieceAtIndex( 56 ) != EPiece::None );
		movePiece<false>( 56, EPiece::BlackRook, 59, None );
		updates.pushSub( 56, EPiece::BlackRook );
		updates.pushAdd( 59, EPiece::BlackRook );
		break;

		// double pawn push
	case DoublePawnPush:
		stateHistory.back().enPassantIndex = static_cast<uint8_t>( end + directionalOffsets[colorToMove] );
		break;

		// en passant
	case EnPassant:
	{
		assert( pieceAtIndex( move.getEndSquare() + directionalOffsets[colorToMove] ) != EPiece::None );
		EPiece pawnPiece = static_cast< EPiece >( EPieceType::Pawn | ( getOtherColorToMove() << 3 ) );
		removePiece<false>( end + directionalOffsets[colorToMove], pawnPiece );
		updates.pushSub( end + directionalOffsets[colorToMove], pawnPiece );
	}
		break;

		// promotion cases
	case promotions[EPieceType::Knight]:
	{
		EPiece knightPiece = static_cast< EPiece >( EPieceType::Knight | ( getColorToMove() << 3 ) );
		addPiece<false>( end, knightPiece );
		updates.pushAdd( end, knightPiece );
	}
		break;

	case promotions[EPieceType::Bishop]:
	{
		EPiece bishopPiece = static_cast< EPiece >( EPieceType::Bishop | ( getColorToMove() << 3 ) );
		addPiece<false>( end, bishopPiece );
		updates.pushAdd( end, bishopPiece );
	}
		break;

	case promotions[EPieceType::Rook]:
	{
		EPiece rookPiece = static_cast< EPiece >( EPieceType::Rook | ( getColorToMove() << 3 ) );
		addPiece<false>( end, rookPiece );
		updates.pushAdd( end, rookPiece );
	}
		break;

	case promotions[EPieceType::Queen]:
	{
		EPiece queenPiece = static_cast< EPiece >( EPieceType::Queen | ( getColorToMove() << 3 ) );
		addPiece<false>( end, queenPiece );
		updates.pushAdd( end, queenPiece );
	}
		break;

	default:
		break;
	}

	plyCount++;
	// if in check, move was illegal
	if( isInCheck() )
	{
		// so you must undo it and return false
		undoMove<false>();
		colorToMove = ( colorToMove == EColor::Black ) ? EColor::White : EColor::Black;
		//std::cout << "Changing Color To Move, move was illegal\n";
		return false;
	}
	else
	{
		if constexpr( UpdateNNUE )
		{
			if constexpr( PushNNUE )
			{
				nnueState.performUpdatesAndPush( updates, stateHistory.back().kingSquares[EColor::Black], stateHistory.back().kingSquares[EColor::White], stateHistory.back() );
			}
			else
			{
				nnueState.performUpdates( updates, stateHistory.back().kingSquares[EColor::Black], stateHistory.back().kingSquares[EColor::White], stateHistory.back() );
			}
		}

		// otherwise it's good, move on
		colorToMove = ( colorToMove == EColor::Black ) ? EColor::White : EColor::Black;
		stateHistory.back().threats = calculateThreats();
		//std::cout << "Changing Color To Move, move was legal\n";
		stateHistory.back().zobristHash ^= zobColorToMove;
		return true;
	}
}

template <bool PushNNUE, bool UpdateNNUE> void Board::undoMove()
{
	//std::cout << "undomove\n";
	stateHistory.pop_back();
	if constexpr( UpdateNNUE && PushNNUE )
	{
		nnueState.pop();
	}

	plyCount--;
	colorToMove = ( colorToMove == EColor::Black ) ? EColor::White : EColor::Black;
	//std::cout << "position fen " << getFenString() << std::endl;
	//std::cout << "Changing Color To Move in undo move\n";
	// no zobrist update here because the saved zobrist hash is before the color changed
}

uint64_t Board::getCurrentPlayerBitboard() const
{
	return stateHistory.back().coloredBitboards[colorToMove];
}

void Board::changeColor()
{
	stateHistory.push_back( stateHistory.back() );
	nnueState.push();
	stateHistory.back().enPassantIndex = 64;
	stateHistory.back().hundredPlyCounter++;
	colorToMove = ( colorToMove == EColor::Black ) ? EColor::White : EColor::Black;
	stateHistory.back().threats = calculateThreats();
	stateHistory.back().zobristHash ^= zobColorToMove;
}

void Board::undoChangeColor()
{
	stateHistory.pop_back();
	nnueState.pop();
	colorToMove = ( colorToMove == EColor::Black ) ? EColor::White : EColor::Black;
}

int32_t Board::getEvaluation() const
{
	//std::cout << "position fen " << getFenString() << std::endl;
	//std::cout << "evaluate" << std::endl;
	int32_t eval = nnueState.evaluate( colorToMove, std::popcount( getOccupiedBitboard() ) );
	//nnueState.fullRefresh(stateHistory.back(), stateHistory.back().kingSquares[0], stateHistory.back().kingSquares[1]);
	//assert(eval == nnueState.evaluate(colorToMove, __builtin_popcountll(getOccupiedBitboard())));
	int32_t phase = 3 * std::popcount( stateHistory.back().pieceBitboards[EPieceType::Knight] )
		+ 3 * std::popcount( stateHistory.back().pieceBitboards[EPieceType::Bishop] )
		+ 5 * std::popcount( stateHistory.back().pieceBitboards[EPieceType::Rook] )
		+ 10 * std::popcount( stateHistory.back().pieceBitboards[EPieceType::Queen] );

	eval = eval * ( 206 + phase ) / 256;
	eval = eval * ( 200 - stateHistory.back().hundredPlyCounter ) / 200;

	return eval;
}

int32_t Board::getCastlingRights() const
{
	return stateHistory.back().castlingRights;
}

int32_t Board::getEnPassantIndex() const
{
	return stateHistory.back().enPassantIndex;
}

void initializeZobrist()
{
	// random number stuff
	//std::random_device rd;
	std::mt19937_64 gen( 0xABBABA5ED );
	std::uniform_int_distribution<uint64_t> dis;
	// color to move
	zobColorToMove = dis( gen );
	// squares
	for( int32_t i = 0; i < 64; i++ )
	{
		for( int32_t j = 0; j < 14; j++ )
		{
			zobTable[i][j] = dis( gen );
		}
	}
}

uint64_t Board::fullZobristRegen()
{
	//std::cout << "Beginning Full Regen\n";
	uint64_t mask = getOccupiedBitboard();
	uint64_t result = 0;
	while( mask != 0 )
	{
		int8_t index = popLSB( mask );
		EPiece piece = pieceAtIndex( index );
		//std::cout << "Adding piece of type " << std::to_string(piece) << " at index " << std::to_string(index) << '\n';
		result ^= zobTable[index][piece];
	}

	if( colorToMove == EColor::White )
	{
		//std::cout << "Changing Color To Move in full regen\n";
		result ^= zobColorToMove;
	}

	//std::cout << "Result: " << std::to_string(result) << '\n';
	return result;
}

bool Board::isRepeatedPosition() const
{
	const int32_t size = static_cast< int32_t >( std::ssize( stateHistory ) );

	// A position loaded from a FEN can carry a halfmove clock far larger than the history we
	// actually hold, so the scan has to stop at the start of the history rather than trusting
	// the counter to bound it.
	const int32_t lowest = std::max( 0, size - static_cast< int32_t >( stateHistory.back().hundredPlyCounter ) - 1 );

	for( int32_t i = size - 5; i >= lowest; i -= 2 )
	{
		if( stateHistory[i].zobristHash == stateHistory.back().zobristHash )
		{
			return true;
		}
	}

	return false;
}

bool Board::isLegalMove( const Move& move ) const
{
	std::array<Move, 256> moves;
	const int32_t totalMoves = getMoves( moves );
	for( int32_t i = 0; i < totalMoves; i++ )
	{
		if( moves[i] == move )
		{
			return true;
		}
	}

	return false;
}

uint64_t Board::getAttackers( int32_t square ) const
{
	uint64_t occupied = getOccupiedBitboard();
	return ( getPawnAttacks( square, EColor::Black ) & getColoredPieceBitboard( EColor::White, EPieceType::Pawn ) )
		| ( getPawnAttacks( square, EColor::White ) & getColoredPieceBitboard( EColor::Black, EPieceType::Pawn ) )
		| ( getKnightAttacks( square ) & stateHistory.back().pieceBitboards[Knight] )
		| ( getRookAttacks( square, occupied ) & ( stateHistory.back().pieceBitboards[EPieceType::Rook] | stateHistory.back().pieceBitboards[EPieceType::Queen] ) )
		| ( getBishopAttacks( square, occupied ) & ( stateHistory.back().pieceBitboards[EPieceType::Bishop] | stateHistory.back().pieceBitboards[EPieceType::Queen] ) )
		| ( getKingAttacks( square ) & stateHistory.back().pieceBitboards[EPieceType::King] );
}

uint64_t Board::getColoredBitboard( int32_t color ) const
{
	return stateHistory.back().coloredBitboards[color];
}

uint64_t Board::getPieceBitboard( int32_t piece ) const
{
	return stateHistory.back().pieceBitboards[piece];
}

int32_t Board::getFiftyMoveCount() const
{
	return stateHistory.back().hundredPlyCounter / 2;
}

uint64_t Board::getZobristHash() const
{
	return stateHistory.back().zobristHash;
}

std::array<int32_t, 2> Board::getNonPawnHash() const
{
	std::array<uint64_t, EColor::ColorCount> hashes = stateHistory.back().nonPawnHashes;
	std::array<int32_t, 2> clipped = { static_cast<int32_t>( hashes[0] & Corrhist::mask ), static_cast<int32_t>( hashes[1] & Corrhist::mask ) };
	return clipped;
}

BoardState Board::getBoardState() const
{
	return stateHistory.back();
}

Board::Board( BoardState s, EColor ctm )
{
	stateHistory.push_back( s );
	colorToMove = ctm;
	stateHistory.back().hundredPlyCounter = 0;
	plyCount = 0;
	stateHistory.reserve( 256 );
}

// estimates the resulting zobrist key from a move
// uint64_t Board::keyAfter( const Move move ) const
// {
// 	const int32_t startSquare = move.getStartSquare();
// 	const int32_t endSquare = move.getEndSquare();
//
// 	const int32_t moving = pieceAtIndex( startSquare );
// 	const int32_t captured = pieceAtIndex( endSquare );
//
// 	uint64_t key = stateHistory.back().zobristHash;
//
// 	key ^= zobTable[startSquare][moving];
// 	key ^= zobTable[endSquare][moving];
//
// 	if( captured != None )
// 	{
// 		key ^= zobTable[endSquare][captured];
// 	}
//
// 	key ^= zobColorToMove;
//
// 	return key;
// }

int32_t Board::getPlyCount() const
{
	return plyCount;
}

bool Board::isPKEndgame() const
{
	uint64_t occupied = getOccupiedBitboard();
	uint64_t pk = getPieceBitboard( EPieceType::Pawn ) | getPieceBitboard( EPieceType::King );
	return ( ( getColoredBitboard( colorToMove ) & ( occupied ^ pk ) ) == 0 );
}

int32_t Board::getPawnHashIndex() const
{
	// last 15 bits
	return stateHistory.back().pawnHash & Corrhist::mask;
}

uint64_t Board::calculateThreats() const
{
	const EColor them = getOtherColorToMove();
	const uint64_t occupied = getOccupiedBitboard();

	uint64_t threats = 0;

	const uint64_t queens = getColoredPieceBitboard( them, EPieceType::Queen );

	uint64_t rooks = queens | getColoredPieceBitboard( them, EPieceType::Rook );
	while( rooks )
	{
		const int8_t rook = popLSB( rooks );
		threats |= getRookAttacks( rook, occupied );
	}

	uint64_t bishops = queens | getColoredPieceBitboard( them, EPieceType::Bishop );
	while( bishops )
	{
		const int8_t bishop = popLSB( bishops );
		threats |= getBishopAttacks( bishop, occupied );
	}

	uint64_t knights = getColoredPieceBitboard( them, EPieceType::Knight );
	while( knights )
	{
		const int8_t knight = popLSB( knights );
		threats |= getKnightAttacks( knight );
	}

	const uint64_t pawns = getColoredPieceBitboard( them, EPieceType::Pawn );
	if( them == 0 )
	{
		threats |= ( ( pawns >> 9 ) & ~getFileMask( 7 ) ) | ( ( pawns >> 7 ) & ~getFileMask( 0 ) );
	}
	else
	{
		threats |= ( ( pawns << 7 ) & ~getFileMask( 7 ) ) | ( ( pawns << 9 ) & ~getFileMask( 0 ) );
	}

	threats |= getKingAttacks( stateHistory.back().kingSquares[them] );

	return threats;
}

uint64_t Board::getThreats() const
{
	return stateHistory.back().threats;
}

int32_t Board::getMajorHash() const
{
	return stateHistory.back().majorHash & Corrhist::mask;
}

int32_t Board::getMinorHash() const
{
	return stateHistory.back().minorHash & Corrhist::mask;
}

bool Board::isPseudolegal( Move move ) const
{
	const int8_t from = move.getStartSquare();
	const int8_t to = move.getEndSquare();
	const int32_t flag = move.getFlag();
	const EPiece piece = pieceAtIndex( from );
	const EPiece victim = pieceAtIndex( to );

	assert( piece < EPiece::PieceCount );
	if( piece == None || getColor( piece ) != colorToMove )
	{
		return false;
	}

	// ignoring another condition that would otherwise go here because my castling moves aren't encoded as king takes rook
	// if capturing own piece or king, that's a bad thing
	if( victim != EPiece::None && ( getColor( victim ) == colorToMove || getType( victim ) == EPieceType::King ) )
	{
		return false;
	}

	if( flag >= castling[0] && flag <= castling[3] )
	{
		if( getType( piece ) != EPieceType::King || isInCheck() )
		{
			return false;
		}

		// if the rank is wrong
		const int32_t home = ( colorToMove == EColor::Black ) ? 7 : 0;
		if( from / 8 != home || to / 8 != home )
		{
			return false;
		}

		// movegen checks
		// Check if any castling rights exist for the current player
		if( ( stateHistory.back().castlingRights & kingRightMasks[getOtherColorToMove()] ) == 0 )
		{
			return false;
		}

		const uint64_t occ = getOccupiedBitboard();
		// Check specific castling move types
		if( colorToMove == EColor::White )
		{ 
			// White to move
			if( from == 4 && to == 6 && flag == castling[0] )
			{ 
				// White kingside castling
				return ( stateHistory.back().castlingRights & 1 ) != 0 && ( occ & 0x60 ) == 0 && !squareIsUnderAttack( 5 );
			}
			else if( from == 4 && to == 2 && flag == castling[1] )
			{ 
				// White queenside castling
				return ( stateHistory.back().castlingRights & 2 ) != 0 && ( occ & 0xE ) == 0 && !squareIsUnderAttack( 3 );
			}
		}
		else
		{ 
			// Black to move
			if( from == 60 && to == 62 && flag == castling[2] )
			{ 
				// Black kingside castling
				return ( stateHistory.back().castlingRights & 4 ) != 0 && ( occ & 0x6000000000000000 ) == 0 && !squareIsUnderAttack( 61 );
			}
			else if( from == 60 && to == 58 && flag == castling[3] )
			{ 
				// Black queenside castling
				return ( stateHistory.back().castlingRights & 8 ) != 0 && ( occ & 0xE00000000000000 ) == 0 && !squareIsUnderAttack( 59 );
			}
		}

		return false;
	}

	if( getType( piece ) == EPieceType::Pawn )
	{
		if( flag == EnPassant )
		{
			return to == stateHistory.back().enPassantIndex && ( getPawnAttacks( from, colorToMove ) & ( 1ULL << to ) ) != 0;
		}

		const int32_t fromRank = from / 8;
		const int32_t toRank = to / 8;
		// moving backwards is bad
		if( ( colorToMove == EColor::Black && toRank >= fromRank ) || ( colorToMove == EColor::White && toRank <= fromRank ) )
		{
			return false;
		}

		const int32_t promoRank = colorToMove == EColor::Black ? 0 : 7;
		// promotion to wrong rank or nonpromotion on back rank
		if( ( flag >= promotions[EPieceType::Knight] ) != ( toRank == promoRank ) )
		{
			return false;
		}

		const int32_t fromFile = from & 7;
		const int32_t toFile = to & 7;
		if( fromFile != toFile )
		{
			const uint64_t pawnAttacks = getPawnAttacks( from, colorToMove );
			const uint64_t enemyPieces = getColoredBitboard( getOtherColorToMove() );
			const uint64_t attackedEnemies = pawnAttacks & enemyPieces;

			if( !( attackedEnemies & ( 1ULL << to ) ) )
			{
				return false;
			}
		}
		else if( victim != None )
		{
			// forward capture
			return false;
		}

		const int32_t rankDelta = std::abs( toRank - fromRank );
		int32_t maxDelta;
		if( colorToMove == EColor::Black )
		{
			maxDelta = fromRank == 6 ? 2 : 1;
		}
		else
		{
			maxDelta = fromRank == 1 ? 2 : 1;
		}

		if( rankDelta > maxDelta )
		{
			return false;
		}

		// incorrect delta
		if( rankDelta == 2 && flag != DoublePawnPush )
		{
			return false;
		}

		if( rankDelta == 1 && flag == DoublePawnPush )
		{
			return false;
		}

		// blocked double pawn push
		if( rankDelta == 2 && ( getOccupiedBitboard() & ( 1ULL << ( to + ( colorToMove == EColor::White ? -8 : 8 ) ) ) ) )
		{
			return false;
		}
	}
	else
	{
		// can't do ep or promote without a pawn
		if( flag == EnPassant || flag >= promotions[EPieceType::Knight] || flag == DoublePawnPush )
		{
			return false;
		}

		uint64_t total = 0ULL;
		const uint64_t occ = getOccupiedBitboard();
		const EPieceType currentType = getType( piece );
		if( currentType == EPieceType::Knight )
		{
			total = getKnightAttacks( from );
		}
		else if( currentType == EPieceType::Bishop )
		{
			total = getBishopAttacks( from, occ );
		}
		else if( currentType == EPieceType::Rook )
		{
			total = getRookAttacks( from, occ );
		}
		else if( currentType == EPieceType::Queen )
		{
			total = getRookAttacks( from, occ ) | getBishopAttacks( from, occ );
		}
		else if( currentType == EPieceType::King )
		{
			total = getKingAttacks( from );
		}
		else
		{
			std::unreachable();
		}

		if( ( total & ( 1ULL << to ) ) == 0 )
		{
			return false;
		}
	}

	return true;
}

// currently only generating captures, should perhaps generate pawn promotions as well but it's fine
int32_t Board::getNoisies( std::array<Move, 256>& moves, int32_t totalMoves ) const
{
	uint64_t occupiedBitboard = getOccupiedBitboard();

	uint64_t mask = stateHistory.back().coloredBitboards[colorToMove] ^ getColoredPieceBitboard( colorToMove, EPieceType::Pawn );
	while( mask != 0 )
	{
		int8_t startSquare = popLSB( mask );
		EPieceType piece = getType( pieceAtIndex( startSquare ) );
		uint64_t total = 0;
		if( piece == EPieceType::Knight )
		{
			total = getKnightAttacks( startSquare );
		}
		else if( piece == EPieceType::Bishop )
		{
			total = getBishopAttacks( startSquare, occupiedBitboard );
		}
		else if( piece == EPieceType::Rook )
		{
			total = getRookAttacks( startSquare, occupiedBitboard );
		}
		else if( piece == EPieceType::Queen )
		{
			total = getRookAttacks( startSquare, occupiedBitboard ) | getBishopAttacks( startSquare, occupiedBitboard );
		}
		else if( piece == EPieceType::King )
		{
			total = getKingAttacks( startSquare );
		}

		total &= stateHistory.back().coloredBitboards[getOtherColorToMove()];
		while( total != 0 )
		{
			moves[totalMoves] = Move( startSquare, popLSB( total ), Normal );
			totalMoves++;
		}
	}

	uint64_t pawnBitboard = getColoredPieceBitboard( colorToMove, EPieceType::Pawn );
	uint64_t capturable = stateHistory.back().coloredBitboards[getOtherColorToMove()];
	if( stateHistory.back().enPassantIndex != 64 )
	{
		capturable |= squareToBitboard[stateHistory.back().enPassantIndex];
	}

	uint64_t leftCaptures = ( colorToMove == EColor::Black ? pawnBitboard >> 9 : pawnBitboard << 7 );
	leftCaptures &= ~getFileMask( 7 );
	leftCaptures &= capturable;
	uint64_t leftCapturePromotions = leftCaptures & getRankMask( 7 * colorToMove );
	leftCaptures ^= leftCapturePromotions;

	uint64_t rightCaptures = ( colorToMove == EColor::Black ? pawnBitboard >> 7 : pawnBitboard << 9 );
	rightCaptures &= ~getFileMask( 0 );
	rightCaptures &= capturable;
	uint64_t rightCapturePromotions = rightCaptures & getRankMask( 7 * colorToMove );
	rightCaptures ^= rightCapturePromotions;

	while( leftCaptures != 0 )
	{
		int8_t index = popLSB( leftCaptures );
		int8_t startSquare = index + ( colorToMove == EColor::Black ? 9 : -7 );
		moves[totalMoves] = Move( startSquare, index, ( index == stateHistory.back().enPassantIndex ? EnPassant : Normal ) );
		totalMoves++;
	}

	while( rightCaptures != 0 )
	{
		int8_t index = popLSB( rightCaptures );
		int8_t startSquare = index + ( colorToMove == EColor::Black ? 7 : -9 );
		moves[totalMoves] = Move( startSquare, index, ( index == stateHistory.back().enPassantIndex ? EnPassant : Normal ) );
		totalMoves++;
	}

	while( leftCapturePromotions != 0 )
	{
		int8_t index = popLSB( leftCapturePromotions );
		int8_t startSquare = index + ( colorToMove == EColor::Black ? 9 : -7 );
		for( int32_t piece = EPieceType::Knight; piece < EPieceType::King; piece++ )
		{
			moves[totalMoves] = Move( startSquare, index, promotions[piece] );
			totalMoves++;
		}
	}

	while( rightCapturePromotions != 0 )
	{
		int8_t index = popLSB( rightCapturePromotions );
		int8_t startSquare = index + ( colorToMove == EColor::Black ? 7 : -9 );
		for( int32_t piece = EPieceType::Knight; piece < EPieceType::King; piece++ )
		{
			moves[totalMoves] = Move( startSquare, index, promotions[piece] );
			totalMoves++;
		}
	}

	return totalMoves;
}

int32_t Board::getQuiets( std::array<Move, 256>& moves, int32_t totalMoves ) const
{
	uint64_t occupiedBitboard = getOccupiedBitboard();

	if( ( stateHistory.back().castlingRights & kingRightMasks[getOtherColorToMove()] ) != 0 )
	{
		if( !isInCheck() )
		{
			if( colorToMove == EColor::White )
			{
				if( ( stateHistory.back().castlingRights & 1 ) != 0 && ( occupiedBitboard & 0x60 ) == 0 && !squareIsUnderAttack( 5 ) )
				{
					moves[totalMoves] = Move( 4, 6, castling[0] );
					totalMoves++;
				}
				if( ( stateHistory.back().castlingRights & 2 ) != 0 && ( occupiedBitboard & 0xE ) == 0 && !squareIsUnderAttack( 3 ) )
				{
					moves[totalMoves] = Move( 4, 2, castling[1] );
					totalMoves++;
				}
			}
			else
			{
				if( ( stateHistory.back().castlingRights & 4 ) != 0 && ( occupiedBitboard & 0x6000000000000000 ) == 0 && !squareIsUnderAttack( 61 ) )
				{
					moves[totalMoves] = Move( 60, 62, castling[2] );
					totalMoves++;
				}
				if( ( stateHistory.back().castlingRights & 8 ) != 0 && ( occupiedBitboard & 0xE00000000000000 ) == 0 && !squareIsUnderAttack( 59 ) )
				{
					moves[totalMoves] = Move( 60, 58, castling[3] );
					totalMoves++;
				}
			}
		}
	}

	uint64_t mask = stateHistory.back().coloredBitboards[colorToMove] ^ getColoredPieceBitboard( colorToMove, EPieceType::Pawn );
	while( mask != 0 )
	{
		int8_t startSquare = popLSB( mask );
		EPieceType pieceType = getType( pieceAtIndex( startSquare ) );
		uint64_t total = 0;
		if( pieceType == EPieceType::Knight )
		{
			total = getKnightAttacks( startSquare );
		}
		else if( pieceType == EPieceType::Bishop )
		{
			total = getBishopAttacks( startSquare, occupiedBitboard );
		}
		else if( pieceType == EPieceType::Rook )
		{
			total = getRookAttacks( startSquare, occupiedBitboard );
		}
		else if( pieceType == EPieceType::Queen )
		{
			total = getRookAttacks( startSquare, occupiedBitboard ) | getBishopAttacks( startSquare, occupiedBitboard );
		}
		else if( pieceType == EPieceType::King )
		{
			total = getKingAttacks( startSquare );
		}

		total &= ~occupiedBitboard;
		while( total != 0 )
		{
			moves[totalMoves] = Move( startSquare, popLSB( total ), Normal );
			totalMoves++;
		}
	}

	// pawn pushes
	uint64_t pawnBitboard = getColoredPieceBitboard( colorToMove, EPieceType::Pawn );
	uint64_t emptyBitboard = ~occupiedBitboard;
	uint64_t pawnPushes = getPawnPushes( pawnBitboard, emptyBitboard, colorToMove );
	uint64_t doublePawnPushes = getDoublePawnPushes( pawnPushes, emptyBitboard, colorToMove );
	uint64_t pawnPushPromotions = pawnPushes & getRankMask( 7 * colorToMove );
	pawnPushes ^= pawnPushPromotions;
	while( pawnPushes != 0 )
	{
		int8_t index = popLSB( pawnPushes );
		int8_t startSquare = index + directionalOffsets[colorToMove];
		moves[totalMoves] = Move( startSquare, index, Normal );
		totalMoves++;
	}

	while( doublePawnPushes != 0 )
	{
		int8_t index = popLSB( doublePawnPushes );
		int8_t startSquare = index + ( directionalOffsets[colorToMove] << 1 );
		assert( getType( pieceAtIndex( startSquare ) ) == EPieceType::Pawn );
		moves[totalMoves] = Move( startSquare, index, DoublePawnPush );
		totalMoves++;
	}

	while( pawnPushPromotions != 0 )
	{
		int8_t index = popLSB( pawnPushPromotions );
		int8_t startSquare = index + directionalOffsets[colorToMove];
		for( int32_t piece = EPieceType::Knight; piece < EPieceType::King; piece++ )
		{
			moves[totalMoves] = Move( startSquare, index, promotions[piece] );
			totalMoves++;
		}
	}

	return totalMoves;
}