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

#include "search.h"

#include <algorithm>
#include "tt.h"
#include "normalize.h"
#include "tbprobe.h"
#include "uci.h"
#include "tunables.h"
#include "movepick.h"

std::atomic<bool> timesUp = false;

bool mainThreadDone = false;

uint64_t hardNodeCap = 400000;

// resets the history, done when ucinewgame is sent, and at the start of each turn
// thanks zzzzz
void Engine::clearHistory()
{
	std::memset( info.historyTable.data(), 0, sizeof( info.historyTable ) );
	std::memset( info.noisyHistoryTable.data(), 0, sizeof( info.noisyHistoryTable ) );
	std::memset( info.conthistTable.get(), 0, sizeof( info.conthistTable ) );
	std::memset( info.qsHistoryTable.data(), 0, sizeof( info.qsHistoryTable ) );
	std::memset( info.pawnHistoryTable.data(), 0, sizeof( info.pawnHistoryTable ) );
	info.corrhist.clear();
}

Move Engine::getBestMove() const
{
	return rootBestMove;
}

// resets the engine, done when ucinewgame is sent
void Engine::resetEngine()
{
	info.stack = {};
	std::memset( nodeTMTable.data(), 0, sizeof( nodeTMTable ) );
	std::memset( info.counterMoves.data(), 0, sizeof( info.counterMoves ) );
	clearHistory();
}

// Quiecense search, searching all the captures until there aren't anymore so that you can get an accurate eval
int16_t Engine::qSearch( Board& board, int32_t alpha, int32_t beta, int16_t ply )
{
	info.stack[ply].pvLength = 0;
	// check if search is supposed to have ended
	if( !limits.hardLimitCheck( nodes, begin ) )
	{
		timesUp.store( true );
		return 0;
	}

	seldepth = std::max<int32_t>( ply, seldepth );

	const uint64_t hash = board.getZobristHash();
	// TT check
	Transposition* entry = TT->getEntry( hash );

	if( entry->zobristKey == shrink( hash )
		&& ( entry->flag == Exact // exact score
		|| ( entry->flag == BetaCutoff && entry->score >= beta ) // lower bound, fail high
		|| ( entry->flag == FailLow && entry->score <= alpha ) // upper bound, fail low
		) )
	{
		return entry->score;
	}

	// stand pat shenanigans
	int32_t staticEval;
	if( entry->zobristKey == shrink( hash ) )
	{
		staticEval = entry->staticEval;
	}
	else
	{
		staticEval = board.getEvaluation();
	}

	if( ply > depthLimit - 1 )
	{
		return static_cast<int16_t>( staticEval );
	}

	// corrections
	const int32_t ctm = board.getColorToMove();
	int32_t chpawnHash = board.getPawnHashIndex() & Corrhist::mask;
	std::array<int32_t, 2> nonPawnHash = board.getNonPawnHash();
	int32_t majorHash = board.getMajorHash();
	int32_t minorHash = board.getMinorHash();
	staticEval = info.corrhist.correct( ctm, chpawnHash, staticEval, nonPawnHash, majorHash, minorHash );

	// adjust staticEval to TT score if it's good enough
	if( shrink( hash ) == entry->zobristKey
		&& ( entry->flag == Exact
		|| ( entry->flag == BetaCutoff && entry->score >= staticEval )
		|| ( entry->flag == FailLow && entry->score <= staticEval )
		) )
	{
		staticEval = entry->score;
	}

	int16_t bestScore = static_cast<int16_t>( staticEval );
	if( bestScore >= beta )
	{
		return bestScore;
	}

	alpha = std::max<int32_t>( alpha, bestScore );

	// values useful for writing to TT later
	Move bestMove;
	int32_t flag = FailLow;

	int32_t legalMoves = 0;
	std::array<Move, 256> testedMoves;
	MovePicker picker = MovePicker::qsearch( board, entry->bestMove, info );
	// loop though all the moves
	while( true )
	{
		Move move = picker.next().first;
		if( !move )
		{
			break;
		}

		// this detects bad captures
		if( !see( board, move, 0 ) )
		{
			continue;
		}

		// TT prefetching
		/*uint64_t afterKey = board.keyAfter( move ); */
		//__builtin_prefetch(TT->getEntry(afterKey));

		// History Pruning
		//if(moveValues[i] < qhpDepthMultiplier.value * qDepth) break;
		if( !board.makeMove<true>( move ) )
		{
			continue;
		}

		testedMoves[legalMoves] = move;
		legalMoves++;
		nodes++;
		// searches from this node
		const int32_t score = -qSearch( board, -beta, -alpha, ply + 1 );
		board.undoMove<true>();
		// time check
		if( timesUp.load( std::memory_order_relaxed ) )
		{
			return 0;
		}

		if( score > bestScore )
		{
			bestScore = static_cast<int16_t>( score );

			// Improve alpha
			if( score > alpha )
			{
				alpha = score;
				bestMove = move;
				info.stack[ply].pvTable[0] = move;
				info.stack[ply].pvLength = info.stack[ply + 1].pvLength + 1;
				for( int32_t i = 0; i < info.stack[ply + 1].pvLength; i++ )
				{
					info.stack[ply].pvTable[i + 1] = info.stack[ply + 1].pvTable[i];
				}
			}

			// Fail-high
			if( score >= beta )
			{
				flag = BetaCutoff;
				bestMove = move;
				// approximating depth using the distance from seldepth
				int32_t qDepth = seldepth - ply;
				int32_t bonus = static_cast<int32_t>( std::min( qhsMaxBonus.value, qhsMultiplier.value * qDepth * qDepth + qhsAdder.value * qDepth - qhsSubtractor.value ) );
				// if it's a capture or queen promotion
				if( move.getFlag() < promotions[0] || move.getFlag() == promotions[3] )
				{
					const int32_t end = move.getEndSquare();
					const int32_t piece = getType( board.pieceAtIndex( move.getStartSquare() ) );
					const int32_t victim = getType( board.pieceAtIndex( end ) );
					updateQSHistory( board.getColorToMove(), piece, end, victim, bonus );
				}

				bonus = -bonus;
				// malus!
				for( int32_t moveNo = 0; moveNo < legalMoves - 1; moveNo++ )
				{
					Move maluMove = testedMoves[moveNo];
					if( maluMove.getFlag() < promotions[0] || maluMove.getFlag() == promotions[3] )
					{
						const int32_t maluEnd = maluMove.getEndSquare();
						const int32_t maluPiece = getType( board.pieceAtIndex( maluMove.getStartSquare() ) );
						const int32_t maluVictim = getType( board.pieceAtIndex( maluEnd ) );
						updateQSHistory( board.getColorToMove(), maluPiece, maluEnd, maluVictim, bonus );
					}
				}
				break;
			}
		}
	}

	// push to TT
	Transposition entryToWrite = Transposition( hash, bestMove, static_cast<uint8_t>( flag ), staticEval, bestScore, 0 );
	TT->setEntry( hash, entryToWrite );

	return bestScore;
}

// adds to the history of a particular move
void Engine::updateHistory( const int32_t colorToMove, const int32_t start, const int32_t end, const int32_t piece, const int32_t bonus, const int16_t ply, const int32_t hash, const bool startAttack, const bool endAttack )
{
	int32_t thingToAdd = bonus - info.historyTable[colorToMove][start][startAttack][end][endAttack] * std::abs( bonus ) / historyCap;
	info.historyTable[colorToMove][start][startAttack][end][endAttack] += static_cast<int16_t>( thingToAdd );

	const int32_t baseConthistScore = ( ply > 0 ? ( *info.stack[ply - 1].ch_entry )[colorToMove][piece][end] : 0 )
		+ ( ply > 1 ? ( *info.stack[ply - 2].ch_entry )[colorToMove][piece][end] : 0 )
		+ ( ply > 3 ? ( *info.stack[ply - 4].ch_entry )[colorToMove][piece][end] : 0 );

	if( ply > 0 )
	{
		thingToAdd = bonus - baseConthistScore * std::abs( bonus ) / historyCap;
		( *info.stack[ply - 1].ch_entry )[colorToMove][piece][end] += static_cast< int16_t >( thingToAdd );
	}

	if( ply > 1 )
	{
		thingToAdd = bonus - baseConthistScore * std::abs( bonus ) / historyCap;
		( *info.stack[ply - 2].ch_entry )[colorToMove][piece][end] += static_cast< int16_t >( thingToAdd );
	}

	if( ply > 3 )
	{
		thingToAdd = bonus - baseConthistScore * std::abs( bonus ) / historyCap;
		( *info.stack[ply - 4].ch_entry )[colorToMove][piece][end] += static_cast< int16_t >( thingToAdd );
	}

	thingToAdd = bonus - info.pawnHistoryTable[hash][colorToMove][piece][end] * std::abs( bonus ) / historyCap;
	info.pawnHistoryTable[hash][colorToMove][piece][end] += static_cast< int16_t >( thingToAdd );
}

void Engine::updateNoisyHistory( const int32_t colorToMove, const int32_t piece, const int32_t end, const int32_t victim, const int32_t bonus, const bool endAttack )
{
	const int32_t thingToAdd = bonus - info.noisyHistoryTable[colorToMove][piece][end][victim][endAttack] * std::abs( bonus ) / historyCap;
	info.noisyHistoryTable[colorToMove][piece][end][victim][endAttack] += static_cast<int16_t>( thingToAdd );
}

void Engine::updateQSHistory( const int32_t colorToMove, const int32_t piece, const int32_t end, const int32_t victim, const int32_t bonus )
{
	const int32_t thingToAdd = bonus - info.qsHistoryTable[colorToMove][piece][end][victim] * std::abs( bonus ) / historyCap;
	info.qsHistoryTable[colorToMove][piece][end][victim] += static_cast< const int16_t >( thingToAdd );
}

// The main search function
int16_t Engine::negamax( Board& board, int32_t depth, int32_t alpha, int32_t beta, int16_t ply, bool nmpAllowed, bool isCutNode )
{
	info.stack[ply].pvLength = 0;
	// if it's a repeated position, it's a draw
	if( ply > 0 && ( board.getFiftyMoveCount() >= 50 || board.isRepeatedPosition() ) )
	{
		return 0;
	}

	// check if search is supposed to have ended
	if( !limits.hardLimitCheck( nodes, begin ) )
	{
		timesUp.store( true );
		return 0;
	}

	const bool inCheck = board.isInCheck();
	info.stack[ply].inCheck = inCheck;
	// activate q search if at the end of a branch
	if( depth <= 0 && !inCheck )
	{
		return qSearch( board, alpha, beta, ply );
	}

	const bool inSingularSearch = info.stack[ply].excluded != Move();
	const bool isPV = beta > alpha + 1;
	const uint64_t hash = board.getZobristHash();

	// TT check
	Transposition* entry = nullptr;

	if( !inSingularSearch )
	{
		entry = TT->getEntry( hash );
	}

	// if it meets these criteria, it's done the search exactly the same way before, if not more throuroughly in the past and you can skip it
	// it would make sense to add !isPV here, however from my testing that makes it about 80 elo worse
	// turns out that score above was complete bs lol, my isPV was broken
	if( !isPV && !inSingularSearch && ply > 0 && entry->zobristKey == shrink( hash ) && entry->depth >= depth
		&& ( entry->flag == Exact // exact score
		|| ( entry->flag == BetaCutoff && entry->score >= beta ) // lower bound, fail high
		|| ( entry->flag == FailLow && entry->score <= alpha ) // upper bound, fail low
		) )
	{
		return entry->score;
	}

	// Internal Iterative Reduction (IIR)
	// Things to test: alternative depth
	if( !inSingularSearch && ( entry->zobristKey != shrink( hash ) || entry->bestMove == Move() ) && depth > iirDepthCondition.value )
	{
		depth--;
	}

	int32_t staticEval = 0;
	int32_t originalStaticEval = 0;
	if( !inSingularSearch && entry->zobristKey == shrink( hash ) )
	{
		staticEval = entry->staticEval;
	}
	else
	{
		staticEval = board.getEvaluation();
		if( !inSingularSearch && entry->zobristKey == 0 )
		{
			Transposition entryToWrite = Transposition( hash, Move(), 0, staticEval, 0, 0 );
			TT->setEntry( hash, entryToWrite );
		}
	}
	originalStaticEval = staticEval;
	if( ply > depthLimit - 1 )
	{
		return static_cast<int16_t>( staticEval );
	}

	// corrections
	const int32_t ctm = board.getColorToMove();
	int32_t chpawnHash = board.getPawnHashIndex() & Corrhist::mask;
	std::array<int32_t, 2> nonPawnHash = board.getNonPawnHash();
	int32_t majorHash = board.getMajorHash();
	int32_t minorHash = board.getMinorHash();
	staticEval = info.corrhist.correct( ctm, chpawnHash, staticEval, nonPawnHash, majorHash, minorHash );
	int32_t correction = staticEval - originalStaticEval;
	bool corrhistUncertain = std::abs( correction ) > chUncertaintyMargin.value;

	info.stack[ply].staticEval = staticEval;
	bool improving = false;
	if( inCheck )
	{
		improving = false;
	}
	else if( ply > 1 && !info.stack[ply - 2].inCheck )
	{
		improving = staticEval > info.stack[ply - 2].staticEval;
	}
	else if( ply > 3 && !info.stack[ply - 4].inCheck )
	{
		improving = staticEval > info.stack[ply - 4].staticEval;
	}
	else
	{
		improving = true;
	}

	bool opponent_worsening = false;
	if( !inCheck && ply > 0 )
	{
		opponent_worsening = info.stack[ply].staticEval + info.stack[ply - 1].staticEval > 1;
	}

	// adjust staticEval to TT score if it's good enough
	if( !inCheck && !inSingularSearch && shrink( hash ) == entry->zobristKey
		&& ( entry->flag == Exact ||
		( entry->flag == BetaCutoff && entry->score >= staticEval ) ||
		( entry->flag == FailLow && entry->score <= staticEval )
		) )
	{
		staticEval = entry->score;
	}

	// Razoring
	if( !isPV && staticEval < alpha - razDepthMultiplier.value * depth )
	{
		int32_t score = qSearch( board, alpha, beta, ply );
		if( score < alpha )
		{
			return static_cast<int16_t>( score );
		}
	}

	if( !isPV && !inCheck && ply > 0 && info.stack[ply - 1].reduction >= 3 && !opponent_worsening )
	{
		++depth;
	}

	// Reverse Futility Pruning
	if( !inSingularSearch && staticEval - rfpMultiplier.value * ( depth - improving ) >= beta && !inCheck && depth < rfpDepthCondition.value && !isPV )
	{
		return static_cast<int16_t>( ( staticEval + beta ) / 2 );
	}

	// Null Move Pruning (NMP)
	// Things to test: !isPV, alternate formulas, etc
	// "I could probably detect zugzwang here but ehhhhh" -Me, a few months ago
	if( !isPV && !inSingularSearch && !board.isPKEndgame() && nmpAllowed && depth >= nmpDepthCondition.value && !inCheck && staticEval >= beta && staticEval >= beta + 175 - 25 * depth )
	{
		info.stack[ply].ch_entry = ( *info.conthistTable )[0][0][0].data();
		info.stack[ply].move = Move();
		board.changeColor();
		const int32_t score = -negamax( board, depth - 3 - depth / 3 - std::min( ( staticEval - beta ) / static_cast<int32_t>( nmpDivisor.value ), static_cast<int32_t>( nmpSubtractor.value ) ), 0 - beta, 1 - beta, ply + 1, false, !isCutNode );
		board.undoChangeColor();
		if( score >= beta )
		{
			return static_cast<int16_t>( score );
		}
	}

	if( ply > 0 )
	{
		info.stack[ply].doubleExtensions = info.stack[ply - 1].doubleExtensions;
	}

	// values useful for writing to TT later
	int32_t bestScore = matedScore;
	Move bestMove;
	bool bestIsCapture = false;
	int32_t flag = FailLow;

	// extensions, currently only extending if you are in check
	depth += inCheck;

	// Mate Distance Pruning (I will test it at some point I swear)
	if( !isPV )
	{
		const int32_t mdAlpha = std::max( alpha, matedScore + ply );
		const int32_t mdBeta = std::min( beta, -matedScore - ply - 1 );
		if( mdAlpha >= mdBeta )
		{
			return static_cast<int16_t>( mdAlpha );
		}
	}
	// capturable squares to determine if a move is a capture.
	const uint64_t capturable = board.getOccupiedBitboard();
	// loop through the moves
	int32_t legalMoves = 0;
	std::array<Move, 256> testedMoves;
	MovePicker picker = MovePicker::search( board, inSingularSearch ? Move() : entry->bestMove, info, ply );
	while( true )
	{
		auto [move, moveValue] = picker.next();
		if( !move )
		{
			break;
		}

		// filter by optimal moves for tb positions
		if( ply == 0 )
		{
			bool found = false;
			for( int32_t i = 0; i < numRootMoves; i++ )
			{
				if( rootMoves[i] == move )
				{
					found = true;
					break;
				}
			}

			if( !found )
			{
				continue;
			}
		}

		if( move == info.stack[ply].excluded )
		{
			continue;
		}

		int32_t moveStartSquare = move.getStartSquare();
		int32_t moveEndSquare = move.getEndSquare();
		bool moveEndAttack = board.squareIsUnderAttack( moveEndSquare );
		int32_t moveFlag = move.getFlag();
		bool isCapture = ( ( capturable & ( 1ULL << moveEndSquare ) ) != 0 ) || moveFlag == EnPassant;
		bool isQuiet = ( !isCapture && ( moveFlag <= DoublePawnPush ) );
		int32_t moveVictim = None;
		if( isCapture )
		{
			moveVictim = getType( board.pieceAtIndex( moveEndSquare ) );
		}

		bool isQuietOrBadCapture = ( moveValue <= historyCap * 5 );

		// move loop prunings:
		// futility pruning
		if( !picker.skipQuiets && bestScore > matedScore && !inCheck && depth <= fpDepthCondition.value && staticEval + fpBase.value + depth * fpMultiplier.value <= alpha )
		{
			picker.skipQuiets = true;
		}

		// Late Move Pruning
		if( !isPV && isQuiet && bestScore > matedScore + 256 && legalMoves > ( lmpBase.value + depth * depth ) / ( 2 - improving ) )
		{
			break;
		}

		// see pruning
		if( depth <= sprDepthCondition.value && isQuietOrBadCapture && bestScore > matedScore + 256 && !see( board, move, static_cast<int32_t>( depth * ( isCapture ? sprCaptureThreshold.value : sprQuietThreshold.value ) )) )
		{
			continue;
		}

		// History Pruning
		if( ply > 0 && !isPV && isQuiet && depth <= hipDepthCondition.value && moveValue < hipDepthMultiplier.value * depth )
		{
			break;
		}

		// TT prefetching
		/*uint64_t afterKey = board.keyAfter( move );*/
		//__builtin_prefetch(TT->getEntry(afterKey));

		int32_t TTExtensions = 0;
		// determine whether or not to extend TT move (Singular Extensions)
		if( !inSingularSearch && entry->bestMove == move && depth >= sinDepthCondition.value && entry->depth >= depth - sinDepthMargin.value && entry->flag != FailLow )
		{
			const short sBeta = std::max( matedScore, int16_t( entry->score - depth * static_cast<int32_t>( sinDepthScale.value ) / 16 ) );
			const int32_t sDepth = ( depth - 1 ) / 2;

			info.stack[ply].excluded = entry->bestMove;
			const int16_t score = negamax( board, sDepth, sBeta - 1, sBeta, ply, true, isCutNode );
			info.stack[ply].excluded = Move();
			if( score < sBeta )
			{
				if( !isPV && score < sBeta - dexMargin.value && info.stack[ply].doubleExtensions <= dexLimit.value )
				{
					TTExtensions = 2 + ( score < sBeta - texMargin.value );
					depth += ( depth < deiDepth.value );
					info.stack[ply].doubleExtensions++;
				}
				else
				{
					TTExtensions = 1;
				}
			}
			else if( sBeta >= beta )
			{
				// multicut!
				return sBeta;
			}
			else if( entry->score >= beta )
			{
				// negative extensions!
				TTExtensions = -2;
			}
			else if( isCutNode )
			{
				TTExtensions = -1;
			}
		}

		if( !board.makeMove<true>( move ) )
		{
			continue;
		}

		info.stack[ply].ch_entry = &( *info.conthistTable )[board.getColorToMove()][getType( board.pieceAtIndex( moveEndSquare ) )][moveEndSquare][moveVictim];
		info.stack[ply].move = move;
		testedMoves[legalMoves] = move;
		legalMoves++;
		nodes++;
		int32_t score = 0;
		// Principal Variation Search
		int32_t presearchNodeCount = static_cast<int32_t>( nodes );
		if( legalMoves == 1 )
		{
			// searches the first move at full depth
			score = -negamax( board, depth - 1 + TTExtensions, -beta, -alpha, ply + 1, true, false );
		}
		else
		{
			// Late Move Reductions (LMR)
			int32_t lmr = 0;
			if( depth > lmrDepth.value )
			{
				lmr = reductions[depth][legalMoves];
				lmr -= isPV;
				if( isQuiet )
				{
					lmr -= moveValue / static_cast<int32_t>( hmrDivisor.value );
				}
				else
				{
					lmr -= info.historyTable[1 - board.getColorToMove()][moveStartSquare][board.squareIsUnderAttack( moveStartSquare )][moveEndSquare][board.squareIsUnderAttack( moveEndSquare )] / static_cast<int32_t>( cmrDivisor.value );
				}
				lmr += isCutNode * 2;
				lmr -= improving;
				lmr -= !isQuietOrBadCapture;
				lmr -= corrhistUncertain;

				lmr = std::clamp( lmr, 0, depth - 1 );
			}

			info.stack[ply].reduction = lmr;
			// this is more PVS stuff, searching with a reduced margin
			score = -negamax( board, depth - lmr - 1, -alpha - 1, -alpha, ply + 1, true, true );
			info.stack[ply].reduction = 0;
			// and then if it fails high or low we search again with the original bounds
			if( score > alpha && ( score < beta || lmr > 0 ) )
			{
				score = -negamax( board, depth - 1, -beta, -alpha, ply + 1, true, false );
			}
		}

		board.undoMove<true>();

		if( ply == 0 )
		{
			nodeTMTable[moveStartSquare][moveEndSquare] += static_cast<int32_t>( nodes - presearchNodeCount );
		}

		// backup time check
		if( timesUp.load( std::memory_order_relaxed ) )
		{
			return 0;
		}

		if( score > bestScore )
		{
			bestScore = score;

			// Improve alpha
			if( score > alpha )
			{
				flag = Exact;
				alpha = score;
				bestMove = move;
				bestIsCapture = isCapture;
				if( ply == 0 )
				{
					rootBestMove = move;
				}

				if( isPV )
				{
					info.stack[ply].pvTable[0] = move;
					info.stack[ply].pvLength = info.stack[ply + 1].pvLength + 1;
					for( int32_t i = 0; i < info.stack[ply + 1].pvLength; i++ )
					{
						info.stack[ply].pvTable[i + 1] = info.stack[ply + 1].pvTable[i];
					}
				}

				// Fail-high
				if( score >= beta )
				{
					flag = BetaCutoff;
					bestMove = move;
					bestIsCapture = isCapture;
					if( ply == 0 )
					{
						rootBestMove = move;
					}
					const int32_t colorToMove = board.getColorToMove();
					// testing berserk history bonus
					int32_t bonus = static_cast<int32_t>( std::min( hstMaxBonus.value, hstAdder.value * depth - hstSubtractor.value ) );
					int32_t hash2 = board.getPawnHashIndex();
					if( isQuiet )
					{
						// adds to the move's history and adjusts the killer move accordingly
						int32_t start = moveStartSquare;
						int32_t end = moveEndSquare;
						int32_t piece = getType( board.pieceAtIndex( start ) );
						updateHistory( colorToMove, start, end, piece, bonus, ply, hash2, board.squareIsUnderAttack( start ), moveEndAttack );
						info.stack[ply].killer = move;
						if( ply > 0 )
						{
							info.counterMoves[info.stack[ply - 1].move.getStartSquare()][info.stack[ply - 1].move.getEndSquare()] = move;
						}
					}
					else if( move.getFlag() < promotions[0] || move.getFlag() == promotions[3] )
					{
						const int32_t end = move.getEndSquare();
						const int32_t piece = getType( board.pieceAtIndex( move.getStartSquare() ) );
						const int32_t victim = getType( board.pieceAtIndex( end ) );
						updateNoisyHistory( board.getColorToMove(), piece, end, victim, bonus, moveEndAttack );
					}
					bonus = -bonus;
					// malus!
					for( int32_t moveNo = 0; moveNo < legalMoves - 1; moveNo++ )
					{
						Move maluMove = testedMoves[moveNo];
						const int32_t start = maluMove.getStartSquare();
						const int32_t end = maluMove.getEndSquare();
						const int32_t flag2 = maluMove.getFlag();
						const int32_t piece = getType( board.pieceAtIndex( start ) );
						bool maluIsCapture = ( ( capturable & ( 1ULL << end ) ) != 0 ) || flag2 == EnPassant;
						bool maluIsQuiet = ( !maluIsCapture && ( flag2 <= DoublePawnPush ) );
						if( maluIsQuiet )
						{
							updateHistory( colorToMove, start, end, piece, bonus, ply, hash2, board.squareIsUnderAttack( start ), board.squareIsUnderAttack( end ) );
						}
						else if( maluMove.getFlag() < promotions[0] || maluMove.getFlag() == promotions[3] )
						{
							const int32_t victim = getType( board.pieceAtIndex( end ) );
							updateNoisyHistory( colorToMove, piece, end, victim, bonus, board.squareIsUnderAttack( end ) );
						}
					}
					break;
				}
			}
		}
	}

	// checkmate / stalemate detection, if I did legal move generation instead of pseudolegal I could probably do this first
	if( legalMoves == 0 )
	{
		if( info.stack[ply].excluded != Move() )
		{
			return static_cast<int16_t>( alpha );
		}
		if( inCheck )
		{
			return matedScore + ply;
		}
		return 0;
	}

	if( !inCheck && ( bestMove == Move() || !bestIsCapture ) && !( bestScore >= beta && bestScore <= staticEval ) && !( bestMove == Move() && bestScore >= staticEval ) )
	{
		info.corrhist.push( chpawnHash, ctm, bestScore, staticEval, depth, nonPawnHash, majorHash, minorHash );
	}

	// push to TT
	if( !inSingularSearch )
	{
		if( entry->zobristKey == shrink( hash ) && entry->bestMove != Move() && bestMove == Move() ) bestMove = entry->bestMove;
		Transposition entryToWrite = Transposition( hash, bestMove, static_cast<uint8_t>( flag ), originalStaticEval, bestScore, static_cast<uint8_t>( depth ) );
		TT->setEntry( hash, entryToWrite );
	}

	return static_cast<int16_t>( bestScore );
}

// fetches the PV line from the table
std::string Engine::getPV()
{
	std::string pv;
	for( int32_t i = 0; i < info.stack[0].pvLength; i++ )
	{
		pv += toLongAlgebraic( info.stack[0].pvTable[i] ) + " ";
	}
	return pv;
}

void Engine::outputInfo( const Board& board, int32_t score, int32_t depth, int32_t elapsedTime )
{
	//std::cout << "root pv length: " << info.stack[0].pvLength << std::endl;
	std::string scoreString = " score ";
	if( abs( score ) < abs( matedScore + 256 ) )
	{
		scoreString += "cp ";
		scoreString += std::to_string( normalize( score, board.getPlyCount() ) );
	}
	else
	{
		// score is checkmate in score - matedScore ply
		// position fen rn1q2rk/pp3p1p/2p4Q/3p4/7P/2NP2R1/PPP3P1/4RK2 w - - 0 1
		// ^^ mate in 3 test position
		int32_t colorMultiplier = score > 0 ? 1 : -1;
		scoreString += "mate ";
		scoreString += std::to_string( ( abs( abs( score ) + matedScore ) / 2 + board.getColorToMove() ) * colorMultiplier );
	}
	uint64_t nodeSum = getTotalNodes();
	std::cout << "info depth " << std::to_string( depth ) << " seldepth " << std::to_string( seldepth ) << " nodes " << std::to_string( nodeSum ) << " time " << std::to_string( elapsedTime ) << " nps " << std::to_string( static_cast<int32_t>( double( nodeSum ) / ( elapsedTime == 0 ? 1 : elapsedTime ) * 1000 ) ) << scoreString << " pv " << getPV() << std::endl;
}

// the usual search function, where you give it the amount of time it has left, and it will search in increasing depth steps until it runs out of time
Move Engine::think( Board board, const SearchLimiters& limiters, bool printInfo, const std::array<Move, 256>& optimalMoves, int32_t numOptimalMoves )
{
	rootMoves = optimalMoves;
	numRootMoves = numOptimalMoves;
	info.stack[0].doubleExtensions = 0;
	limits = limiters;
	//ageHistory();
	//clearHistory();
	std::memset( nodeTMTable.data(), 0, sizeof( nodeTMTable ) );
	nodes = 0;
	seldepth = 0;
	timesUp.store( false );
	int32_t stability = 0;

	begin = std::chrono::steady_clock::now();

	rootBestMove = Move();
	int16_t score = 0;

	// Iterative Deepening, searches to increasing depths, which sounds like it would slow things down but it makes it much better
	for( int32_t depth = 1; depth <= limits.getDepthLimit(); depth++ )
	{
		// Aspiration Windows, searches with reduced bounds until it doesn't fail high or low
		seldepth = depth;
		int32_t delta = static_cast<int32_t>( aspBaseDelta.value );
		int32_t alpha = std::max( static_cast<int32_t>( matedScore ), score - delta );
		int32_t beta = std::min( -matedScore, score + delta );
		const Move previousBest = rootBestMove;
		int32_t usedDepth = depth;
		if( depth > aspDepthCondition.value )
		{
			while( true )
			{
				score = negamax( board, usedDepth, alpha, beta, 0, true, false );
				if( rootBestMove == previousBest )
				{
					stability++;
				}
				else
				{
					stability = 0;
				}

				if( timesUp.load( std::memory_order_relaxed ) )
				{
					break;
				}

				if( score >= beta )
				{
					beta = std::min( beta + delta, -matedScore );
					usedDepth = std::max( usedDepth - 1, depth - 5 );
				}
				else if( score <= alpha )
				{
					beta = ( alpha + beta ) / 2;
					alpha = std::max( alpha - delta, static_cast<int32_t>( matedScore ) );
					usedDepth = depth;
				}
				else
				{
					break;
				}

				delta = static_cast<int32_t>( delta * aspDeltaMultiplier.value );
			}
		}
		else
		{
			score = negamax( board, depth, matedScore, -matedScore, 0, true, false );
		}

		if( timesUp.load( std::memory_order_relaxed ) )
		{
			rootBestMove = previousBest;
		}

		if( printInfo )
		{
			const int64_t elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - begin ).count();
			outputInfo( board, score, depth, static_cast<int32_t>( elapsedTime ) );
			// soft time bounds check
			if( !limits.softLimitCheck( elapsedTime, rootBestMove, nodes, nodeTMTable, depth, stability ) )
			{
				break;
			}
		}
	}

	if( rootBestMove == Move() )
	{
		MovePicker picker = MovePicker::search( board, Move(), info, 0 );

		while( true )
		{
			auto [move, score2] = picker.next();
			if( !move )
			{
				break;
			}

			if( board.makeMove<true>( move ) )
			{
				board.undoMove<true>();
				rootBestMove = move;
				break;
			}
		}
	}

	if( printInfo )
	{
		timesUp.store( true );
		stopOtherThreads();
		std::cout << "bestmove " << toLongAlgebraic( rootBestMove ) << std::endl;
		mainThreadDone = true;
	}

	return rootBestMove;
}
