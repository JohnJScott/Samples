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

#include <iostream>
#include <cstdint>
#include <cassert>
#include <string_view>
#include <array>
#include <ranges>
#include <algorithm>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <fstream>
#include <thread>

#include "nwupdate.h"

extern bool useSyzygy;

// Note: WHITE, BLACK values are reverse of Stockfish
enum EColor
	: int8_t
{
	Black,
	White,
	ColorCount
};

// Each type of piece. Or'd with 8 for white pieces. Starting at 1 to match the PieceType enum.
enum EPieceType
	: int8_t
{
	NoneType = 0,
	Pawn,
	Knight,
	Bishop,
	Rook,
	Queen,
	King,
	PieceTypeCount
};

enum EPiece
	: int8_t
{
	None = 0,

	BlackPawn = 1,
	BlackKnight,
	BlackBishop,
	BlackRook,
	BlackQueen,
	BlackKing,

	WhitePawn = 9,
	WhiteKnight,
	WhiteBishop,
	WhiteRook,
	WhiteQueen,
	WhiteKing,

	PieceCount
};

// declaring things i guess
enum flags
{
	Undefined,
	FailLow,
	BetaCutoff,
	Exact
};

constexpr int8_t numRanks = 8;
constexpr int8_t numFiles = 8;
constexpr int8_t numBoardSquares = numRanks * numFiles;
constexpr int32_t ColorStride = 64 * 6;

static constexpr uint64_t corrHistSize = 16384;
static constexpr uint64_t corrHistMask = corrHistSize - 1;

constexpr std::array<std::string_view, 64> squareNames =
{
	"a1","b1","c1","d1","e1","f1","g1","h1",
	"a2","b2","c2","d2","e2","f2","g2","h2",
	"a3","b3","c3","d3","e3","f3","g3","h3",
	"a4","b4","c4","d4","e4","f4","g4","h4",
	"a5","b5","c5","d5","e5","f5","g5","h5",
	"a6","b6","c6","d6","e6","f6","g6","h6",
	"a7","b7","c7","d7","e7","f7","g7","h7",
	"a8","b8","c8","d8","e8","f8","g8","h8",
};

extern std::array<std::array<uint8_t, 218>, 150> reductions;

// structs and stuff
struct Board;

// holds the state of the board, specifically the factors that can't be brought back after a move is made
struct BoardState
{
	std::array<uint64_t, EPieceType::PieceTypeCount> pieceBitboards;
	std::array<uint64_t, EColor::ColorCount> coloredBitboards;
	std::array<uint64_t, EColor::ColorCount> nonPawnHashes;

	std::array<EPiece, numBoardSquares> mailbox;
	std::array<int8_t, EColor::ColorCount> kingSquares;

	uint64_t zobristHash;
	uint64_t pawnHash;
	uint64_t majorHash;
	uint64_t minorHash;
	uint64_t threats;

	int8_t enPassantIndex;
	uint8_t hundredPlyCounter;
	uint8_t castlingRights;
};

inline EColor getOtherColor( EColor color )
{
	return static_cast< EColor >( 1 - color );
}

// a single move, stored in 16 bits
struct Move
{
public:
	Move( int8_t startSquare, int8_t endSquare, int32_t flag );
	Move()
	{
		value = 0;
	}

	Move( const std::string& longAlgebraic, const Board& board );
	Move( int8_t startSquare, int8_t endSquare, int32_t promote, int32_t ep, const Board& board );

	[[nodiscard]] constexpr bool operator==( const Move& other ) const = default;
	[[nodiscard]] explicit constexpr operator bool() const
	{
		return value != 0;
	}

	int8_t getStartSquare() const
	{
		return value & 0b111111;
	}

	int8_t getEndSquare() const
	{
		return ( value >> 6 ) & 0b111111;
	}

	uint16_t getFlag() const
	{
		return value >> 12;
	}
private:
	uint16_t value;
};

/*
Current Net: cn_028
Arch: (768x8->1024)x2->1x8
Activation: SCReLU
Special Details:
 - Horizontal Mirroring
 - Few more buckets, let's see if we can get some improvement from finny tables now
*/
constexpr int32_t inputSize = 768;
constexpr int32_t inputBucketCount = 8;
constexpr int32_t layer1Size = 1024;
constexpr int32_t outputBucketCount = 8;

constexpr std::array<int32_t, 64> inputBuckets = []
{
	constexpr std::array<int32_t, 32> rawInputBuckets =
	{
		0, 1, 2, 3,
		4, 4, 5, 5,
		6, 6, 6, 6,
		6, 6, 6, 6,
		7, 7, 7, 7,
		7, 7, 7, 7,
		7, 7, 7, 7,
		7, 7, 7, 7
	};

	std::array<int32_t, 64> result = {};

	for( int32_t rank = 0; rank < 8; rank++ )
	{
		for( int32_t file = 0; file < 4; file++ )
		{
			const int32_t src = rank * 4 + file;
			const int32_t dst = rank * 8 + file;

			result[dst] = rawInputBuckets[src];
			result[dst ^ 7] = rawInputBuckets[src] + inputBucketCount;
		}
	}

	return result;
}( );

// organizing this somewhat similarly to code I've seen, mostly from clarity_sp_nnue, made by Ciekce.
constexpr int32_t alignmentAmount = 256;

#pragma warning( push )
#pragma warning( disable : 4324 )
struct alignas( alignmentAmount ) Network
{
	std::array<int16_t, inputSize * inputBucketCount * layer1Size> featureWeights;
	std::array<int16_t, layer1Size> featureBiases;
	std::array<int16_t, layer1Size * 2 * outputBucketCount> outputWeights;
	std::array<int16_t, outputBucketCount> outputBiases;
};
#pragma warning( pop )

static_assert( offsetof( Network, featureWeights ) % alignmentAmount == 0 );
static_assert( offsetof( Network, featureBiases ) % alignmentAmount == 0 );
static_assert( offsetof( Network, outputWeights ) % alignmentAmount == 0 );
static_assert( offsetof( Network, outputBiases ) % alignmentAmount == 0 );

struct Accumulator
{
	alignas( alignmentAmount ) std::array<std::int16_t, layer1Size> black;
	alignas( alignmentAmount ) std::array<std::int16_t, layer1Size> white;
};

struct RefreshTableEntry
{
	Accumulator accumulator;
	std::array<BoardState, 2> boards{};

	BoardState& colorBoards( EColor color )
	{
		return boards[color];
	}
};

struct RefreshTable
{
	std::vector<RefreshTableEntry> table;

	void init();
};

extern const Network* network;

class NetworkState
{
public:
	NetworkState()
	{
		loadNetwork();
		stack.resize( 128 );
		reset();
	}

	static void loadNetwork()
	{
		if( network == nullptr )
		{
			std::string path = "Data/cn_031.nnue";
			std::ifstream stream( path, std::ios::binary | std::ios::ate );
			stream.unsetf( std::ios::skipws );
			const std::streamsize size = stream.tellg();
			stream.seekg( 0, std::ios::beg );

			network = static_cast<const Network*>( _aligned_malloc( size, 256 ) );
			stream.read( const_cast< char* >( reinterpret_cast< const char* >( network ) ), size );
			stream.close();
		}
	}

	inline void push()
	{
		stack[current + 1] = stack[current];
		current++;
	}

	void performUpdates( const NetworkUpdates& updates, int32_t blackKingSquare, int32_t whiteKingSquare, const BoardState& state );
	void performUpdatesAndPush( const NetworkUpdates& updates, int32_t blackKingSquare, int32_t whiteKingSquare, const BoardState& state );
	inline void pop()
	{
		current--;
	}

	void reset();
	void activateFeature( int32_t square, int32_t type, int32_t blackKingSquare, int32_t whiteKingSquare );
	void activateFeatureSingle( int32_t square, EPieceType pieceType, EColor pieceColor, EColor color, int32_t king );
	void activateFeatureAndPush( int32_t square, int32_t type, int32_t blackKing, int32_t whiteKing );
	void disableFeature( int32_t square, int32_t type, int32_t blackKingSquare, int32_t whiteKingSquare );
	void disableFeatureSingle( int32_t square, EPieceType pieceType, EColor pieceColor, EColor color, int32_t king );
	void refreshAccumulator( EColor  color, const BoardState& state, int32_t kingSquare );
	int32_t evaluate( EColor color, int32_t materialCount ) const;
	void fullRefresh( const BoardState& state, int32_t blackKing, int32_t whiteKing );
	void halfRefresh( EColor color, const BoardState& state, int32_t king );

	// pure functions of their arguments, public so they can be covered directly by tests
	static int32_t getBlackFeatureIndex( int32_t square, EPieceType pieceType, int32_t colorOffset, int32_t king );
	static int32_t getWhiteFeatureIndex( int32_t square, EPieceType pieceType, int32_t colorOffset, int32_t king );

private:
	RefreshTable refreshTable;
	int32_t current;
	std::vector<Accumulator> stack;
};

constexpr bool refreshRequired( int32_t color, int32_t oldKingSquare, int32_t newKingSquare )
{
	if( color == 0 )
	{
		oldKingSquare ^= 56;
		newKingSquare ^= 56;
	}

	return inputBuckets[oldKingSquare] != inputBuckets[newKingSquare];
}

inline int8_t getSquare( const int8_t rank, const int8_t file )
{
	return static_cast< int8_t >( ( ( rank - 1 ) << 3 ) | static_cast< int32_t >( file ) );
}

// the board itself
struct Board
{
public:
	Board( const std::string& fen );
	Board( BoardState s, EColor ctm );

	// UpdateNNUE = false skips the accumulator work entirely, for callers such as perft that
	// count nodes and never evaluate. PushNNUE is ignored when UpdateNNUE is false.
	template <bool PushNNUE, bool UpdateNNUE = true> bool makeMove( Move move );
	template <bool PushNNUE, bool UpdateNNUE = true> void undoMove();
	void displayLetters() const;
	void displayPieces() const;
	int32_t getMoves( std::array<Move, 256>& moves ) const;
	int32_t getMovesQSearch( std::array<Move, 256>& moves ) const;
	std::string getFenString() const;
	bool isInCheck() const;
	bool squareIsUnderAttack( int32_t square ) const;
	void toString() const;

	uint64_t getCurrentPlayerBitboard() const;
	uint64_t getOccupiedBitboard() const;
	uint64_t getColoredPieceBitboard( EColor color, EPieceType piece ) const;

	bool blackToMove() const
	{
		return colorToMove == EColor::Black;
	}

	bool whiteToMove() const
	{
		return colorToMove == EColor::White;
	}

	EColor getColorToMove() const
	{
		return colorToMove;
	}

	EColor getOtherColorToMove() const
	{
		return static_cast<EColor>( 1 - colorToMove );
	}

	EPiece pieceAtIndex( int8_t index ) const
	{
		return stateHistory.back().mailbox[index];
	}

	void changeColor();
	void undoChangeColor();
	int32_t getEvaluation() const;

	int32_t getCastlingRights() const
	{
		return stateHistory.back().castlingRights;
	}

	int32_t getEnPassantIndex() const
	{
		return stateHistory.back().enPassantIndex;
	}

	uint64_t fullZobristRegen();
	bool isRepeatedPosition() const;
	bool isLegalMove( const Move& move ) const;
	uint64_t getAttackers( int8_t square ) const;

	uint64_t getColoredBitboard( EColor color ) const
	{
		return stateHistory.back().coloredBitboards[color];
	}

	uint64_t getPieceTypeBitboard( EPieceType piece ) const
	{
		return stateHistory.back().pieceBitboards[piece];
	}

	int32_t getFiftyMoveCount() const
	{
		return stateHistory.back().hundredPlyCounter / 2;
	}

	uint64_t getZobristHash() const
	{
		return stateHistory.back().zobristHash;
	}

	std::array<uint32_t, 2> getNonPawnHash() const
	{
		std::array<uint32_t, 2> clipped =
		{
			static_cast<uint32_t>( stateHistory.back().nonPawnHashes[EColor::Black] & corrHistMask ), 
			static_cast<uint32_t>( stateHistory.back().nonPawnHashes[EColor::White] & corrHistMask )
		};
		return clipped;
	}

	uint32_t getMajorHash() const
	{
		return stateHistory.back().majorHash & corrHistMask;
	}

	uint32_t getMinorHash() const
	{
		return stateHistory.back().minorHash & corrHistMask;
	}

	BoardState getBoardState() const
	{
		return stateHistory.back();
	}

	// uint64_t keyAfter( const Move move ) const;

	int32_t getPlyCount() const
	{
		return plyCount;
	}

	bool isPKEndgame() const;

	uint32_t getPawnHashIndex() const
	{
		// last 15 bits
		return stateHistory.back().pawnHash & corrHistMask;
	}

	uint64_t getThreats() const
	{
		return stateHistory.back().threats;
	}

	uint64_t calculateThreats() const;
	bool isPseudolegal( Move move ) const;
	int32_t getNoisies( std::array<Move, 256>& moves, int32_t totalMoves ) const;
	int32_t getQuiets( std::array<Move, 256>& moves, int32_t totalMoves ) const;
private:
	int32_t plyCount;
	EColor colorToMove;
	std::vector<BoardState> stateHistory;
	NetworkState nnueState;
	template <bool UpdateNNUE> void addPiece( int8_t square, EPiece piece );
	template <bool UpdateNNUE> void removePiece( int8_t square, EPiece piece );
	template <bool UpdateNNUE> void movePiece( int8_t square1, EPiece piece1, int8_t square2, EPiece piece2 );
};

// the eternal functions, can be used everywhere
// Takes the piece number and gets the piece type (enum EPieceType)
[[nodiscard]] inline EPieceType getType( int32_t piece )
{
	return static_cast<EPieceType>( piece & 7 );
}

// Takes a piece number and gets the color of it
[[nodiscard]] inline EColor getColor( int32_t piece )
{
	return static_cast<EColor>( piece >> 3 );
}

// Takes a piece colour and gets its offset within the white perspective's feature block:
// our pieces occupy the first colour block and theirs the second. Takes EColor rather than a
// piece number so that passing a packed piece by mistake is a compile error, not silence.
[[nodiscard]] inline int32_t getWhiteColorOffset( EColor pieceColor )
{
	return ( pieceColor == EColor::White ) ? 0 : ColorStride;
}

// The same for the black perspective, where black is the side that owns the first block.
[[nodiscard]] inline int32_t getBlackColorOffset( EColor pieceColor )
{
	return ( pieceColor == EColor::Black ) ? 0 : ColorStride;
}

// finds the least significant bit in the uint64_t, gets rid of it, and returns its index
[[nodiscard]] inline int8_t popLSB( uint64_t& bitboard )
{
	int8_t lsb = static_cast<int8_t>( std::countr_zero( bitboard ) );
	bitboard &= bitboard - 1;
	return lsb;
}

[[nodiscard]] std::string toLongAlgebraic( Move move );

[[nodiscard]] inline uint64_t getRankMask( int32_t rank )
{
	static constexpr uint64_t rankMask = 0b11111111;
	return rankMask << ( rank << 3 );
}

[[nodiscard]] inline uint64_t getFileMask( int32_t file )
{
	// a mask for a single file on the board
	static constexpr uint64_t fileMask = 0b100000001000000010000000100000001000000010000000100000001;
	return fileMask << file;
}

[[nodiscard]] uint64_t getRookAttacksOld( int32_t square, uint64_t occupiedBitboard );
[[nodiscard]] uint64_t getBishopAttacksOld( int32_t square, uint64_t occupiedBitboard );
[[nodiscard]] uint64_t getRookAttacks( int32_t square, uint64_t occupiedBitboard );
[[nodiscard]] uint64_t getBishopAttacks( int32_t square, uint64_t occupiedBitboard );
[[nodiscard]] uint64_t getPawnPushes( uint64_t pawnBitboard, uint64_t emptyBitboard, EColor colorToMove );
[[nodiscard]] uint64_t getDoublePawnPushes( uint64_t pawnAttacks, uint64_t emptyBitboard, EColor colorToMove );
[[nodiscard]] uint64_t getPawnAttacks( int32_t square, EColor colorToMove );
[[nodiscard]] uint64_t getKnightAttacks( int32_t square );
[[nodiscard]] uint64_t getKingAttacks( int32_t square );
void initializeZobrist();
void initialize();
std::vector<std::string> split( const std::string string, const char separator );
void sortMoves( std::array<int32_t, 256>& values, std::array<Move, 256>& moves, int32_t numMoves );
void incrementalSort( std::array<int32_t, 256>& values, std::array<Move, 256>& moves, int32_t numMoves, int32_t i );

// flips the index vertically, used for indexing the psqts
[[nodiscard]] inline int32_t flipIndex( int32_t index )
{
	return index ^ 56;
}

void calculateReductions();
uint64_t getPassedPawnMask( int32_t square, int32_t colorToMove );
extern std::array<uint64_t, 64> squareToBitboard;

// conthist hehe
using CHEntry = std::array<std::array<std::array<int16_t, 64>, 7>, 2>;
using CHTable = std::array<std::array<std::array<std::array<CHEntry, 7>, 64>, 7>, 2>;

// flags for moves
constexpr uint8_t Normal = 0b0000;
constexpr std::array<uint8_t, 4> castling = { 0b0001, 0b0010, 0b0011, 0b0100 };
constexpr uint8_t EnPassant = 0b0101;
constexpr uint8_t DoublePawnPush = 0b0110;
constexpr std::array<uint8_t, EPieceType::PieceTypeCount> promotions = { 0b0000, 0b0000, 0b0111, 0b1000, 0b1001, 0b1010, 0b0000 };
