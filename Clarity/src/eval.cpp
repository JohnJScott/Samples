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
#include "globals.h"
#include "simd.h"

const Network* network = nullptr;

constexpr uint32_t ColorStride = 64 * 6;
constexpr uint32_t PieceStride = 64;
constexpr int32_t Scale = 400;
constexpr int32_t Qa = 256;
constexpr int32_t Qb = 64;
constexpr int32_t Qab = Qa * Qb;

void NetworkState::reset()
{
	// stack is a std::vector, so sizeof gives the vector object rather than its buffer.
	std::memset( stack.data(), 0, stack.size() * sizeof( Accumulator ) );
	current = 0;
	refreshTable.init();

	std::ranges::copy( network->featureBiases, stack[current].black.begin() );
	std::ranges::copy( network->featureBiases, stack[current].white.begin() );
}

int32_t NetworkState::getBlackFeatureIndex( int32_t square, int32_t piece, int32_t king )
{
	square ^= 56;
	king ^= 56;

	if( ( king & 7 ) > 3 )
	{
		square ^= 7;
		king ^= 7;
	}

	// colour is keyed on the piece itself, not the perspective: our pieces occupy the first
	// colour block and theirs the second, so both halves share weights.
	const int32_t colorOffset = ( getColor( piece ) == EColor::WHITE ) ? ColorStride : 0;

	return ( inputBuckets[king] * inputSize ) + colorOffset + ( getType( piece ) * PieceStride ) + square;
}

int32_t NetworkState::getWhiteFeatureIndex( int32_t square, int32_t piece, int32_t king )
{
	if( ( king & 7 ) > 3 )
	{
		square ^= 7;
		king ^= 7;
	}

	// colour is keyed on the piece itself, not the perspective: our pieces occupy the first
	// colour block and theirs the second, so both halves share weights.
	const int32_t colorOffset = ( getColor( piece ) == EColor::WHITE ) ? 0 : ColorStride;

	return ( inputBuckets[king] * inputSize ) + colorOffset + ( getType( piece ) * PieceStride ) + square;
}

void NetworkState::activateFeatureSingle( int32_t square, int32_t piece, EColor color, int32_t king )
{
	const int16_t* const __restrict weights = network->featureWeights.data();

	if( color == EColor::BLACK )
	{
		const int32_t index = getBlackFeatureIndex( square, piece, king );
		int16_t* __restrict acc = stack[current].black.data();
		const int16_t* const __restrict w = weights + ( index * layer1Size );

		for( int32_t i = 0; i < layer1Size; ++i )
		{
			acc[i] += w[i];
		}
	}
	else
	{
		const int32_t index = getWhiteFeatureIndex( square, piece, king );
		int16_t* __restrict acc = stack[current].white.data();
		const int16_t* const __restrict w = weights + ( index * layer1Size );

		for( int32_t i = 0; i < layer1Size; ++i )
		{
			acc[i] += w[i];
		}
	}
}

void NetworkState::disableFeatureSingle( int32_t square, int32_t piece, EColor color, int32_t king )
{
	const int16_t* const __restrict weights = network->featureWeights.data();

	if( color == EColor::BLACK )
	{
		const int32_t index = getBlackFeatureIndex( square, piece, king );
		int16_t* __restrict acc = stack[current].black.data();
		const int16_t* const __restrict w = weights + ( index * layer1Size );

		for( int32_t i = 0; i < layer1Size; ++i )
		{
			acc[i] -= w[i];
		}
	}
	else
	{
		const int32_t index = getWhiteFeatureIndex( square, piece, king );
		int16_t* __restrict acc = stack[current].white.data();
		const int16_t* const __restrict w = weights + ( index * layer1Size );

		for( int32_t i = 0; i < layer1Size; ++i )
		{
			acc[i] -= w[i];
		}
	}
}

void NetworkState::performUpdates( const NetworkUpdates& updates, int32_t blackKing, int32_t whiteKing, const BoardState& state )
{
	assert( updates.numAdds <= 2 );
	assert( updates.numSubs <= 2 );
	if( updates.bucketChange )
	{
		EColor color = static_cast< EColor >( 1 - updates.bucketUpdate.piece );

		for( int32_t i = 0; i < updates.numAdds; i++ )
		{
			activateFeatureSingle( updates.adds[i].square, updates.adds[i].piece, color, updates.bucketUpdate.piece == 1 ? blackKing : whiteKing );
		}

		for( int32_t i = 0; i < updates.numSubs; i++ )
		{
			disableFeatureSingle( updates.subs[i].square, updates.subs[i].piece, color, updates.bucketUpdate.piece == 1 ? blackKing : whiteKing );
		}

		refreshAccumulator( static_cast< EColor >( updates.bucketUpdate.piece /*being used here to store color*/ ) /*being used here to store color*/, state, updates.bucketUpdate.piece == 0 ? blackKing : whiteKing );
	}
	else
	{
		for( int32_t i = 0; i < updates.numAdds; i++ )
		{
			activateFeature( updates.adds[i].square, updates.adds[i].piece, blackKing, whiteKing );
		}

		for( int32_t i = 0; i < updates.numSubs; i++ )
		{
			disableFeature( updates.subs[i].square, updates.subs[i].piece, blackKing, whiteKing );
		}
	}
}
void NetworkState::performUpdatesAndPush( const NetworkUpdates& updates, int32_t blackKing, int32_t whiteKing, const BoardState& state )
{
	assert( updates.numAdds <= 2 );
	assert( updates.numSubs <= 2 );
	if( updates.bucketChange )
	{
		if( updates.bucketUpdate.piece == 0 )
		{
			stack[current + 1].white = stack[current].white;
		}
		else
		{
			stack[current + 1].black = stack[current].black;
		}

		current++;

		EColor color = static_cast< EColor >( 1 - updates.bucketUpdate.piece );

		for( int32_t i = 0; i < updates.numAdds; i++ )
		{
			activateFeatureSingle( updates.adds[i].square, updates.adds[i].piece, color, updates.bucketUpdate.piece == 1 ? blackKing : whiteKing );
		}

		for( int32_t i = 0; i < updates.numSubs; i++ )
		{
			disableFeatureSingle( updates.subs[i].square, updates.subs[i].piece, color, updates.bucketUpdate.piece == 1 ? blackKing : whiteKing );
		}

		refreshAccumulator( static_cast< EColor >( updates.bucketUpdate.piece /*being used here to store color*/ ) /*being used here to store color*/, state, updates.bucketUpdate.piece == 0 ? blackKing : whiteKing );
	}
	else
	{
		activateFeatureAndPush( updates.adds[0].square, updates.adds[0].piece, blackKing, whiteKing );
		for( int32_t i = 1; i < updates.numAdds; i++ )
		{
			activateFeature( updates.adds[i].square, updates.adds[i].piece, blackKing, whiteKing );
		}

		for( int32_t i = 0; i < updates.numSubs; i++ )
		{
			disableFeature( updates.subs[i].square, updates.subs[i].piece, blackKing, whiteKing );
		}
	}
}

static int32_t getIBucket( EColor color, int32_t king )
{
	if( color == EColor::BLACK )
	{
		king ^= 56;
	}

	return inputBuckets[king];
}

void NetworkState::refreshAccumulator( EColor color, const BoardState& state, int32_t king )
{
	const int32_t bucket = getIBucket( color, king );

	RefreshTableEntry& entry = refreshTable.table[bucket];
	BoardState& prevBoards = entry.colorBoards( color );

	// the diff below is against prevBoards, which is what entry.accumulator corresponds to,
	// so the updates have to land in the table entry - it is copied into the stack afterwards.
	const int16_t* const __restrict weights = network->featureWeights.data();
	int16_t* const __restrict acc = ( color == EColor::BLACK ) ? entry.accumulator.black.data() : entry.accumulator.white.data();

	for( int32_t piece = Pieces::Pawn; piece < Pieces::None; ++piece )
	{
		for( int32_t c = 0; c < 2; c++ )
		{
			const uint64_t prev = prevBoards.pieceBitboards[piece] & prevBoards.coloredBitboards[c];
			const uint64_t curr = state.pieceBitboards[piece] & state.coloredBitboards[c];

			uint64_t added = curr & ~prev;
			uint64_t removed = prev & ~curr;

			while( added )
			{
				const int32_t sq = popLSB( added );
				const int32_t totalPiece = ( c << 3 ) + piece;
				const int32_t index = ( color == EColor::BLACK )
					? getBlackFeatureIndex( sq, totalPiece, king )
					: getWhiteFeatureIndex( sq, totalPiece, king );
				const int16_t* const __restrict w = weights + ( index * layer1Size );

				for( int32_t i = 0; i < layer1Size; ++i )
				{
					acc[i] += w[i];
				}
			}

			while( removed )
			{
				const int32_t sq = popLSB( removed );
				const int32_t totalPiece = ( c << 3 ) + piece;
				const int32_t index = ( color == EColor::BLACK )
					? getBlackFeatureIndex( sq, totalPiece, king )
					: getWhiteFeatureIndex( sq, totalPiece, king );
				const int16_t* const __restrict w = weights + ( index * layer1Size );

				for( int32_t i = 0; i < layer1Size; ++i )
				{
					acc[i] -= w[i];
				}
			}
		}
	}

	if( color == EColor::BLACK )
	{
		std::memcpy( &stack[current].black, &entry.accumulator.black, sizeof( std::array<int16_t, layer1Size> ) );
	}
	else
	{
		std::memcpy( &stack[current].white, &entry.accumulator.white, sizeof( std::array<int16_t, layer1Size> ) );
	}

	std::memcpy( &prevBoards, &state, sizeof( BoardState ) );
}

void NetworkState::halfRefresh( EColor color, const BoardState& state, int32_t king )
{
	for( int32_t c = 0; c < 2; c++ )
	{
		for( int32_t piece = 0; piece < 6; piece++ )
		{
			uint64_t bitboard = state.pieceBitboards[piece] & state.coloredBitboards[c];

			while( bitboard != 0 )
			{
				int32_t index = popLSB( bitboard );
				int32_t totalPiece = ( c << 3 ) + piece;
				activateFeatureSingle( index, totalPiece, color, king );
			}
		}
	}
}

void NetworkState::fullRefresh( const BoardState& state, int32_t blackKing, int32_t whiteKing )
{
	std::ranges::copy( network->featureBiases, stack[current].black.begin() );
	halfRefresh( EColor::BLACK, state, blackKing );

	std::ranges::copy( network->featureBiases, stack[current].white.begin() );
	halfRefresh( EColor::WHITE, state, whiteKing );
}

void RefreshTable::init()
{
	table.clear();
	table.resize( inputBucketCount * 2 );
	for( int32_t i = 0; i < inputBucketCount * 2; i++ )
	{
		std::ranges::copy( network->featureBiases, table[i].accumulator.black.begin() );
		std::ranges::copy( network->featureBiases, table[i].accumulator.white.begin() );
		std::memset( table[i].boards.data(), 0, sizeof( BoardState ) * 2 );
	}
}

static int32_t getBucket( int32_t pieceCount )
{
	constexpr int32_t divisor = ( 32 + outputBucketCount - 1 ) / outputBucketCount;
	return ( pieceCount - 2 ) / divisor;
}

/*
	A technique that I am using here was invented yesterday (as of writing this) by SomeLizard, developer of the engine Lizard
	I am using the SCReLU activation function, which is CReLU(x)^2 * W
	the technique is to use CReLU(x) * w * CReLU(x) which allows you (assuming weight is in (-127, 127))
	to fit the resulting number in a 16 bit integer, allowing you to perform the remaining functions
	on twice as many numbers at once, leading to a pretty sizeable speedup
*/

int32_t NetworkState::forward( const int32_t bucket, const std::array<int16_t, layer1Size> us, const std::array<int16_t, layer1Size> them, const std::array<const int16_t, layer1Size * 2 * outputBucketCount> weights )
{
	const int32_t bucketIncrement = 2 * layer1Size * bucket;
	const Vector zero = simd_zero();
	const Vector maxVal = simd_set1_epi16( Qa );

	Vector sum = simd_zero();

	for( int32_t i = 0; i < layer1Size / weightsPerVector; ++i )
	{
		// us
		Vector u = simd_load( reinterpret_cast< const Vector* >( &us[i * weightsPerVector] ) );
		u = simd_max_epi16( simd_min_epi16( u, maxVal ), zero );
		Vector uw = simd_load( reinterpret_cast< const Vector* >( &weights[i * weightsPerVector + bucketIncrement] ) );
		sum = simd_add_epi32( sum, simd_madd_epi16( u, simd_mullo_epi16( u, uw ) ) );

		// them
		Vector t = simd_load( reinterpret_cast< const Vector* >( &them[i * weightsPerVector] ) );
		t = simd_max_epi16( simd_min_epi16( t, maxVal ), zero );
		Vector tw = simd_load( reinterpret_cast< const Vector* >( &weights[layer1Size + i * weightsPerVector + bucketIncrement] ) );
		sum = simd_add_epi32( sum, simd_madd_epi16( t, simd_mullo_epi16( t, tw ) ) );
	}

	return simd_reduce_add_epi32( sum );
}

void NetworkState::activateFeature( int32_t square, int32_t piece, int32_t blackKing, int32_t whiteKing )
{
	activateFeatureSingle( square, piece, EColor::BLACK, blackKing );
	activateFeatureSingle( square, piece, EColor::WHITE, whiteKing );
}

void NetworkState::activateFeatureAndPush( int32_t square, int32_t piece, int32_t blackKing, int32_t whiteKing )
{
	int32_t blackIdx = getBlackFeatureIndex( square, piece, blackKing );
	int32_t whiteIdx = getWhiteFeatureIndex( square, piece, whiteKing );

	const int16_t* const __restrict weights = network->featureWeights.data();

	const int16_t* const __restrict blackPrev = stack[current].black.data();
	const int16_t* const __restrict whitePrev = stack[current].white.data();

	int16_t* __restrict blackNext = stack[current + 1].black.data();
	int16_t* __restrict whiteNext = stack[current + 1].white.data();

	for( int32_t i = 0; i < layer1Size; ++i )
	{
		blackNext[i] = blackPrev[i] + weights[blackIdx * layer1Size + i];
		whiteNext[i] = whitePrev[i] + weights[whiteIdx * layer1Size + i];
	}

	current++;
}

void NetworkState::disableFeature( int32_t square, int32_t piece, int32_t blackKing, int32_t whiteKing )
{
	disableFeatureSingle( square, piece, EColor::BLACK, blackKing );
	disableFeatureSingle( square, piece, EColor::WHITE, whiteKing );
}

// todo: lazy updates (oh no)
int32_t NetworkState::evaluate( EColor color, int32_t materialCount ) const
{
	const int32_t bucket = getBucket( materialCount );
	const int32_t output = color == EColor::BLACK
		? forward( bucket, stack[current].black, stack[current].white, network->outputWeights )
		: forward( bucket, stack[current].white, stack[current].black, network->outputWeights );
	return ( output / Qa + network->outputBiases[bucket] ) * Scale / Qab;
}