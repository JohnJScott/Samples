/*
	Clarity
	Copyright (C) 2026 Joseph Pasfield

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See thfe
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "globals.h"
#include "testessentials.h"
#include "search.h"
#include "tt.h"
#include "tbprobe.h"
#include "uci.h"
#include "tunables.h"

/*
	The entirety of my implementation of UCI, read the standard for that if you want more information
	This implementation is currently incomplete, but it is functional
*/
Board board( "8/8/8/8/8/8/8/8 w - - 0 1" );
TranspositionTable TT;
std::vector<Engine> engines;
std::vector<std::jthread> threads;
int32_t threadCount = 1;
int64_t moveOverhead = 10;
bool useSyzygy = false;
uint32_t syzygyProbeLimit = 0;
int32_t rootColorToMove;

// resets everything
static void newGame()
{
	engines.clear();
	engines.reserve( threadCount );
	for( int32_t i = 0; i < threadCount; i++ )
	{
		engines.emplace_back( &TT );
		engines[i].resetEngine();
	}

	TT.clearTable( threadCount );
	board = Board( "8/8/8/8/8/8/8/8 w - - 0 1" );
}

static void setOption( const std::vector<std::string>& tokens )
{
	std::string name = tokens[2];
	if( name == "Hash" )
	{
		uint64_t newSizeMB = std::stoi( tokens[4] );
		TT.resize( newSizeMB, threadCount );
	}
	else if( name == "Threads" )
	{
		threadCount = std::stoi( tokens[4] );
		newGame();
	}
	else if( name == "MoveOverhead" )
	{
		moveOverhead = std::stoi( tokens[4] );
	}
	else if( name == "SyzygyPath" )
	{
		bool initSuccess = tb_init( tokens[4].c_str() );
		useSyzygy = initSuccess;
	}
	else if( name == "SyzygyProbeLimit" )
	{
		syzygyProbeLimit = std::stoul( tokens[4] );
	}
	else
	{
		adjustTunable( name, static_cast<int32_t>( std::stod( tokens[4] ) ) );
	}
}

// loads a position, either startpos, kiwipete(not part of uci but IO just felt like I should), or from a fen string
static void loadPosition( const std::vector<std::string>& tokens )
{
	if( tokens[1] == "startpos" )
	{
		board = Board( "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" );
		for( int32_t i = 3; i < std::ssize( tokens ); i++ )
		{
			board.makeMove<false>( Move( tokens[i], board ) );
		}
	}
	else if( tokens[1] == "fen" )
	{
		board = Board( tokens[2] + " " + tokens[3] + " " + tokens[4] + " " + tokens[5] + " " + tokens[6] + " " + tokens[7] );
		for( int32_t i = 9; i < std::ssize( tokens ); i++ )
		{
			board.makeMove<false>( Move( tokens[i], board ) );
		}
	}
	else if( tokens[1] == "kiwipete" )
	{
		board = Board( "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1" );
		for( int32_t i = 3; i < std::ssize( tokens ); i++ )
		{
			board.makeMove<false>( Move( tokens[i], board ) );
		}
	}
	else
	{
		std::cout << "invalid position command\n";
	}

	rootColorToMove = board.getColorToMove();
}

// has the engine identify itself when the GUI says uci
static void identify()
{
	std::cout << "id name Clarity V8.0.0" << "\n";
	std::cout << "id author Vast" << "\n";
	std::cout << "option name Hash type spin default 64 min 1 max 524288" << "\n";
	std::cout << "option name Threads type spin default 1 min 1 max 16384" << "\n";
	std::cout << "option name MoveOverhead type spin default 10 min 1 max 100000" << "\n";
	std::cout << "option name SyzygyPath type string default <empty>" << "\n";
	// todo: increase max to 8
	std::cout << "option name SyzygyProbeLimit type spin default 0 min 0 max 7" << "\n";
	//outputTunables();
	std::cout << "uciok" << "\n";
}

// tells the engine to search, with support for a few different types
static void go( std::vector<std::string> tokens )
{
	std::array<Move, 256> optimalMoves = {};
	int32_t numOptimalMoves = 0;
	if( useSyzygy )
	{
		if( std::popcount( board.getOccupiedBitboard() ) <= static_cast<int32_t>( std::min( TB_LARGEST, syzygyProbeLimit ) ) )
		{
			// probe endgame tt at root
			std::array<uint32_t, TB_MAX_MOVES> results = {};
			uint32_t probeResult = tb_probe_root(
					board.getColoredBitboard( EColor::White ),
				board.getColoredBitboard( EColor::Black ),
				board.getPieceTypeBitboard( EPieceType::King ),
				board.getPieceTypeBitboard( EPieceType::Queen ),
				board.getPieceTypeBitboard( EPieceType::Rook ),
				board.getPieceTypeBitboard( EPieceType::Bishop ),
				board.getPieceTypeBitboard( EPieceType::Knight ),
				board.getPieceTypeBitboard( EPieceType::Pawn ),
				board.getFiftyMoveCount() << 1,
				0,
				board.getEnPassantIndex() == 64 ? 0 : board.getEnPassantIndex(),
				board.getColorToMove(),
				results.data() );

			if( probeResult != TB_RESULT_FAILED )
			{
				// sort moves by game result
				int32_t numMoves = 0;
				for( int32_t i = 0; i < TB_MAX_MOVES; i++ )
				{
					if( results[i] == TB_RESULT_FAILED )
					{
						numMoves = i;
						break;
					}

					int32_t bestIdx = i;
					for( int32_t j = i + 1; j < TB_MAX_MOVES; j++ )
					{
						if( results[j] == TB_RESULT_FAILED )
						{
							break;
						}

						if( TB_GET_WDL( results[j] ) > TB_GET_WDL( results[bestIdx] ) )
						{
							bestIdx = j;
						}
					}

					if( bestIdx != i )
					{
						std::swap( results[bestIdx], results[i] );
					}
				}

				// convert optimal moves into moves
				uint32_t bestResult = TB_GET_WDL( results[0] );
				for( int32_t i = 0; i < numMoves; i++ )
				{
					if( TB_GET_WDL( results[i] ) != bestResult )
					{
						break;
					}

					int8_t start = TB_GET_FROM( results[i] );
					int8_t end = (int8_t)TB_GET_TO( results[i] );
					int32_t promotion = TB_GET_PROMOTES( results[i] );
					int32_t ep = TB_GET_EP( results[i] );
					optimalMoves[numOptimalMoves++] = Move( start, end, promotion, ep, board );
				}
			}
		}
	}

	// if position not in syzygy, root searches all moves
	if( numOptimalMoves == 0 )
	{
		numOptimalMoves = board.getMoves( optimalMoves );
	}

	int32_t time = 0;
	int32_t depth = 0;
	int32_t inc = 0;
	int32_t movestogo = 20;
	int32_t nodes = 0;
	for( int32_t i = 1; i < std::ssize( tokens ); i += 2 )
	{
		if( tokens[i] == "wtime" && board.getColorToMove() == 1 )
		{
			time = std::stoi( tokens[i + 1] );
		}
		if( tokens[i] == "btime" && board.getColorToMove() == 0 )
		{
			time = std::stoi( tokens[i + 1] );
		}
		if( tokens[i] == "depth" )
		{
			depth = std::stoi( tokens[i + 1] );
		}
		if( tokens[i] == "movestogo" )
		{
			movestogo = std::stoi( tokens[i + 1] );
		}
		if( tokens[i] == "winc" && board.getColorToMove() == 1 )
		{
			inc = std::stoi( tokens[i + 1] );
		}
		if( tokens[i] == "binc" && board.getColorToMove() == 0 )
		{
			inc = std::stoi( tokens[i + 1] );
		}
		if( tokens[i] == "nodes" )
		{
			nodes = std::stoi( tokens[i + 1] );
		}
	}

	SearchLimiters limits = {};
	limits.writeValues( time, inc, movestogo, depth, nodes, 0 );
	for( int32_t i = 0; i < threadCount; i++ )
	{
		threads.emplace_back( [limits, i, optimalMoves, numOptimalMoves]
		{
			engines[i].think( board, limits, i == 0, optimalMoves, numOptimalMoves );
		} );
	}
}

uint64_t getTotalNodes()
{
	uint64_t sum = 0;
	for( const Engine& engine : engines )
	{
		sum += engine.nodes;
	}

	return sum;
}

static void stopThePresses()
{
	timesUp.store( true );
	for( int32_t i = 0; i < threadCount; i++ )
	{
		if( threads[i].joinable() )
		{
			threads[i].join();
		}
	}

	threads.clear();
}

void stopOtherThreads()
{
	for( uint32_t i = 1; i < threads.size(); i++ )
	{
		if( threads[i].joinable() )
		{
			threads[i].join();
		}
	}
}

// interprets the command
static void interpretCommand( const std::string& command )
{
	std::vector<std::string> tokens = split( command, ' ' );

	if( tokens.empty() )
	{
		return;
	}
	
	if( tokens[0] == "printstate" )
	{
		board.toString();
	}
	else if( tokens[0] == "position" )
	{
		loadPosition( tokens );
	}
	else if( tokens[0] == "isready" )
	{
		std::cout << "readyok\n";
	}
	else if( tokens[0] == "uci" )
	{
		identify();
	}
	else if( tokens[0] == "go" )
	{
		go( tokens );
	}
	else if( tokens[0] == "ucinewgame" )
	{
		newGame();
	}
	else if( tokens[0] == "setoption" )
	{
		setOption( tokens );
	}
	else if( tokens[0] == "incheck" )
	{
		std::cout << std::to_string( board.isInCheck() ) << '\n';
	}
	else if( tokens[0] == "getfen" )
	{
		std::cout << board.getFenString() << '\n';
	}
	else if( tokens[0] == "perft" )
	{
		individualPerft( board, std::stoi( tokens[1] ) );
	}
	else if( tokens[0] == "splitperft" )
	{
		splitPerft( board, std::stoi( tokens[1] ) );
	}
	else if( tokens[0] == "evaluate" )
	{
		std::cout << "evaluation " << board.getEvaluation() << '\n';
	}
	else if( tokens[0] == "makemove" )
	{
		board.makeMove<true>( Move( tokens[1], board ) );
	}
	else if( tokens[0] == "undomove" )
	{
		board.undoMove<true>();
	}
	else if( tokens[0] == "nullmove" )
	{
		board.changeColor();
	}
	else if( tokens[0] == "undonullmove" )
	{
		board.undoChangeColor();
	}
	else if( tokens[0] == "isrepeated" )
	{
		std::cout << board.isRepeatedPosition() << '\n';
	}
	else if( tokens[0] == "tunablejson" )
	{
		outputTunableJSON();
	}
	else if( tokens[0] == "tunableob" )
	{
		outputTunableOB();
	}
	else if( tokens[0] == "stop" )
	{
		stopThePresses();
	}
	else if( tokens[0] == "showthreats" )
	{
		std::cout << board.getThreats() << std::endl;
	}
	else if( tokens[0] == "calcthreats" )
	{
		std::cout << board.calculateThreats() << std::endl;
	}
	else
	{
		std::cout << "invalid or unsupported command\n";
	}
}

int32_t main( [[maybe_unused]] int32_t argc, [[maybe_unused]] char* argv[] )
{
	initialize();
	newGame();
	std::cout << std::boolalpha;

	std::string command;
	std::cout << "Input command: " << "\n";
	while( true )
	{
		std::getline( std::cin, command, '\n' );
		if( mainThreadDone && !threads.empty() )
		{
			threads.clear();
		}

		if( command == "quit" )
		{
			if( !threads.empty() )
			{
				stopThePresses();
			}

			return 0;
		}

		interpretCommand( command );
	}

	return 0;
}
