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
#include "eval_ispc.h"

const Network* network = nullptr;

constexpr int32_t PieceStride = 64;
constexpr int32_t Scale = 400;
constexpr int32_t Qa = 256;
constexpr int32_t Qb = 64;
constexpr int32_t Qab = Qa * Qb;

void NetworkState::reset()
{
	current = 0;
	refreshTable.init();

	std::ranges::copy( network->featureBiases, stack[current].black.begin() );
	std::ranges::copy( network->featureBiases, stack[current].white.begin() );
}

int32_t NetworkState::getWhiteFeatureIndex( int32_t square, EPieceType pieceType, int32_t colorOffset, int32_t king )
{
	if( ( king & 7 ) > 3 )
	{
		square ^= 7;
		king ^= 7;
	}

	return ( inputBuckets[king] * inputSize ) + colorOffset + ( ( pieceType - 1 ) * PieceStride ) + square;
}

int32_t NetworkState::getBlackFeatureIndex( int32_t square, EPieceType pieceType, int32_t colorOffset, int32_t king )
{
	square ^= 56;
	king ^= 56;

	if( ( king & 7 ) > 3 )
	{
		square ^= 7;
		king ^= 7;
	}

	return ( inputBuckets[king] * inputSize ) + colorOffset + ( ( pieceType - 1 ) * PieceStride ) + square;
}

void NetworkState::activateFeatureSingle( int32_t square, EPieceType pieceType, EColor pieceColor, EColor color, int32_t king )
{
	const int16_t* const weights = network->featureWeights.data();

	if( color == EColor::Black )
	{
		const int32_t index = getBlackFeatureIndex( square, pieceType, getBlackColorOffset( pieceColor ), king );
		ispc::ActivateFeatureSingle_ispc( stack[current].black.data(), weights, index, layer1Size );
	}
	else
	{
		const int32_t index = getWhiteFeatureIndex( square, pieceType, getWhiteColorOffset( pieceColor ), king );
		ispc::ActivateFeatureSingle_ispc( stack[current].white.data(), weights, index, layer1Size );
	}
}

void NetworkState::disableFeatureSingle( int32_t square, EPieceType pieceType, EColor pieceColor, EColor color, int32_t king )
{
	const int16_t* const weights = network->featureWeights.data();

	if( color == EColor::Black )
	{
		const int32_t index = getBlackFeatureIndex( square, pieceType, getBlackColorOffset( pieceColor ), king );
		ispc::DisableFeatureSingle_ispc( stack[current].black.data(), weights, index, layer1Size );
	}
	else
	{
		const int32_t index = getWhiteFeatureIndex( square, pieceType, getWhiteColorOffset( pieceColor ), king );
		ispc::DisableFeatureSingle_ispc( stack[current].white.data(), weights, index, layer1Size );
	}
}

void NetworkState::performUpdates( const NetworkUpdates& updates, int32_t blackKingSquare, int32_t whiteKingSquare, const BoardState& state )
{
	assert( updates.numAdds <= 2 );
	assert( updates.numSubs <= 2 );
	if( updates.bucketChange )
	{
		const EColor color = static_cast< EColor >( 1 - updates.bucketUpdate.piece );
		
		for( int32_t i = 0; i < updates.numAdds; i++ )
		{
			activateFeatureSingle( updates.adds[i].square, getType( updates.adds[i].piece ), getColor( updates.adds[i].piece ), color, updates.bucketUpdate.piece == 1 ? blackKingSquare : whiteKingSquare );
		}

		for( int32_t i = 0; i < updates.numSubs; i++ )
		{
			disableFeatureSingle( updates.subs[i].square, getType( updates.subs[i].piece ), getColor( updates.subs[i].piece ), color, updates.bucketUpdate.piece == 1 ? blackKingSquare : whiteKingSquare );
		}

		refreshAccumulator( static_cast<EColor>( updates.bucketUpdate.piece /*being used here to store color*/ ), state, updates.bucketUpdate.piece == 0 ? blackKingSquare : whiteKingSquare );
	}
	else
	{
		for( int32_t i = 0; i < updates.numAdds; i++ )
		{
			activateFeature( updates.adds[i].square, updates.adds[i].piece, blackKingSquare, whiteKingSquare );
		}

		for( int32_t i = 0; i < updates.numSubs; i++ )
		{
			disableFeature( updates.subs[i].square, updates.subs[i].piece, blackKingSquare, whiteKingSquare );
		}
	}
}

void NetworkState::performUpdatesAndPush( const NetworkUpdates& updates, int32_t blackKingSquare, int32_t whiteKingSquare, const BoardState& state )
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

		const EColor color = static_cast< EColor >( 1 - updates.bucketUpdate.piece );

		for( int32_t i = 0; i < updates.numAdds; i++ )
		{
			activateFeatureSingle( updates.adds[i].square, getType( updates.adds[i].piece ), getColor( updates.adds[i].piece ), color, updates.bucketUpdate.piece == 1 ? blackKingSquare : whiteKingSquare );
		}

		for( int32_t i = 0; i < updates.numSubs; i++ )
		{
			disableFeatureSingle( updates.subs[i].square, getType( updates.subs[i].piece ), getColor( updates.subs[i].piece ), color, updates.bucketUpdate.piece == 1 ? blackKingSquare : whiteKingSquare );
		}

		refreshAccumulator( static_cast<EColor>( updates.bucketUpdate.piece /*being used here to store color*/ ), state, updates.bucketUpdate.piece == 0 ? blackKingSquare : whiteKingSquare );
	}
	else
	{
		activateFeatureAndPush( updates.adds[0].square, updates.adds[0].piece, blackKingSquare, whiteKingSquare );

		for( int32_t i = 1; i < updates.numAdds; i++ )
		{
			activateFeature( updates.adds[i].square, updates.adds[i].piece, blackKingSquare, whiteKingSquare );
		}

		for( int32_t i = 0; i < updates.numSubs; i++ )
		{
			disableFeature( updates.subs[i].square, updates.subs[i].piece, blackKingSquare, whiteKingSquare );
		}
	}
}

void NetworkState::refreshAccumulator( EColor color, const BoardState& state, int32_t kingSquare )
{
	const int32_t bucket = ( color == EColor::Black ) ? inputBuckets[kingSquare ^ 56] : inputBuckets[kingSquare];

	RefreshTableEntry& entry = refreshTable.table[bucket];
	BoardState& prevBoards = entry.colorBoards( color );

	// the diff below is against prevBoards, which is what entry.accumulator corresponds to,
	// so the updates must land in the table entry - it is copied into the stack afterwards.
	const int16_t* const weights = network->featureWeights.data();
	int16_t* const acc = ( color == EColor::Black ) ? entry.accumulator.black.data() : entry.accumulator.white.data();

	for( const EPieceType pieceType : { EPieceType::Pawn, EPieceType::Knight, EPieceType::Bishop, EPieceType::Rook, EPieceType::Queen, EPieceType::King } )
	{
		for( const EColor pieceColor : { EColor::Black, EColor::White } )
		{
			const int32_t blackColorOffset = getBlackColorOffset( pieceColor );
			const int32_t whiteColorOffset = getWhiteColorOffset( pieceColor );

			const uint64_t prev = prevBoards.pieceBitboards[pieceType] & prevBoards.coloredBitboards[pieceColor];
			const uint64_t curr = state.pieceBitboards[pieceType] & state.coloredBitboards[pieceColor];

			uint64_t added = curr & ~prev;
			uint64_t removed = prev & ~curr;

			while( added )
			{
				const int8_t square = popLSB( added );
				const int32_t index = ( color == EColor::Black )
					? getBlackFeatureIndex( square, pieceType, blackColorOffset, kingSquare )
					: getWhiteFeatureIndex( square, pieceType, whiteColorOffset, kingSquare );

				ispc::ActivateFeatureSingle_ispc( acc, weights, index, layer1Size );
			}

			while( removed )
			{
				const int8_t square = popLSB( removed );
				const int32_t index = ( color == EColor::Black )
					? getBlackFeatureIndex( square, pieceType, blackColorOffset, kingSquare )
					: getWhiteFeatureIndex( square, pieceType, whiteColorOffset, kingSquare );

				ispc::DisableFeatureSingle_ispc( acc, weights, index, layer1Size );
			}
		}
	}

	if( color == EColor::Black )
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
	for( const EColor pieceColor : { EColor::Black, EColor::White } )
	{
		for( const EPieceType pieceType : { EPieceType::Pawn, EPieceType::Knight, EPieceType::Bishop, EPieceType::Rook, EPieceType::Queen, EPieceType::King } )
		{
			uint64_t bitboard = state.pieceBitboards[pieceType] & state.coloredBitboards[pieceColor];

			while( bitboard != 0 )
			{
				int8_t index = popLSB( bitboard );
				activateFeatureSingle( index, pieceType, pieceColor, color, king );
			}
		}
	}
}

void NetworkState::fullRefresh( const BoardState& state, int32_t blackKing, int32_t whiteKing )
{
	std::ranges::copy( network->featureBiases, stack[current].black.begin() );
	halfRefresh( EColor::Black, state, blackKing );

	std::ranges::copy( network->featureBiases, stack[current].white.begin() );
	halfRefresh( EColor::White, state, whiteKing );
}

void RefreshTable::init()
{
	table.clear();
	table.resize( inputBucketCount << 1 );

	for( int32_t i = 0; i < inputBucketCount << 1; i++ )
	{
		std::ranges::copy( network->featureBiases, table[i].accumulator.black.begin() );
		std::ranges::copy( network->featureBiases, table[i].accumulator.white.begin() );
		std::memset( table[i].boards.data(), 0, sizeof( BoardState ) << 1 );
	}
}

void NetworkState::activateFeature( int32_t square, int32_t piece, int32_t blackKingSquare, int32_t whiteKingSquare )
{
	const int16_t* const weights = network->featureWeights.data();
	const EPieceType pieceType = getType( piece );
	const EColor pieceColor = getColor( piece );

	const int32_t blackIndex = getBlackFeatureIndex( square, pieceType, getBlackColorOffset( pieceColor ), blackKingSquare );
	ispc::ActivateFeatureSingle_ispc( stack[current].black.data(), weights, blackIndex, layer1Size );
	
	const int32_t whiteIndex = getWhiteFeatureIndex( square, pieceType, getWhiteColorOffset( pieceColor ), whiteKingSquare );
	ispc::ActivateFeatureSingle_ispc( stack[current].white.data(), weights, whiteIndex, layer1Size );
}

void NetworkState::activateFeatureAndPush( int32_t square, int32_t piece, int32_t blackKing, int32_t whiteKing )
{
	const int16_t* const weights = network->featureWeights.data();
	
	ispc::ActivateFeatureAndPush_ispc( stack[current + 1].black.data(), stack[current].black.data(), getBlackFeatureIndex( square, getType( piece ), getBlackColorOffset( getColor( piece ) ), blackKing ),
										stack[current + 1].white.data(), stack[current].white.data(), getWhiteFeatureIndex( square, getType( piece ), getWhiteColorOffset( getColor( piece ) ), whiteKing ),
										weights, layer1Size );

	current++;
}

void NetworkState::disableFeature( int32_t square, int32_t piece, int32_t blackKingSquare, int32_t whiteKingSquare )
{
	const int16_t* const weights = network->featureWeights.data();
	const EPieceType pieceType = getType( piece );
	const EColor pieceColor = getColor( piece );

	const int32_t blackIndex = getBlackFeatureIndex( square, pieceType, getBlackColorOffset( pieceColor ), blackKingSquare );
	ispc::DisableFeatureSingle_ispc( stack[current].black.data(), weights, blackIndex, layer1Size );

	const int32_t whiteIndex = getWhiteFeatureIndex( square, pieceType, getWhiteColorOffset( pieceColor ), whiteKingSquare );
	ispc::DisableFeatureSingle_ispc( stack[current].white.data(), weights, whiteIndex, layer1Size );
}

// todo: lazy updates (oh no)
int32_t NetworkState::evaluate( EColor color, int32_t materialCount ) const
{
	static constexpr int32_t divisor = ( 32 + outputBucketCount - 1 ) / outputBucketCount;

	const int32_t bucket = ( materialCount - 2 ) / divisor;
	const int32_t bucketIncrement = ( layer1Size * bucket ) << 1;
	const int32_t output = color == EColor::Black
		? ispc::Forward_ispc( stack[current].black.data(), stack[current].white.data(), network->outputWeights.data(), bucketIncrement, layer1Size, Qa )
		: ispc::Forward_ispc( stack[current].white.data(), stack[current].black.data(), network->outputWeights.data(), bucketIncrement, layer1Size, Qa );

	return ( output / Qa + network->outputBiases[bucket] ) * Scale / Qab;
}