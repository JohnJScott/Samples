// Copyright Eternal Developments, LLC. All rights reserved.

#include <chrono>
#include <memory>
#include <string>

#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "../src/globals.h"
#include "../src/search.h"
#include "../src/testsuites.h"
#include "../src/tt.h"
#include "../src/uci.h"

// ClarityCommon's search calls back into the host process for thread coordination, which the
// UCI executable normally supplies. The test host runs one engine on one thread, so stopping
// the other threads is a no-op and the node total is simply that engine's own count.
static Engine* activeBenchEngine = nullptr;

void stopOtherThreads()
{
}

uint64_t getTotalNodes()
{
	return ( activeBenchEngine != nullptr ) ? activeBenchEngine->nodes : 0;
}

namespace ClarityTest
{
#define TEST_METHOD_CATEGORY( test_name, category_name ) \
	BEGIN_TEST_METHOD_ATTRIBUTE( test_name ) \
		TEST_METHOD_ATTRIBUTE( L"Category", L#category_name ) \
		TEST_PRIORITY( 12 ) \
	END_TEST_METHOD_ATTRIBUTE() \
	TEST_METHOD( test_name )

// The native adapter does not expose method attributes to vstest.console's /TestCaseFilter,
// so long-running tests carry a Slow prefix in their name instead. Routine runs use
// /TestCaseFilter:"Name!~Slow"; the Category attribute above still groups them in the
// Visual Studio Test Explorer.

	// fens from Stormphrax, which are from alexandria, which are ultimately from bitgenie
	// I also have now added 3 fens from endgame.epd from OB
	static const std::array<std::string, 53> benchFens =
	{
		"r3k2r/2pb1ppp/2pp1q2/p7/1nP1B3/1P2P3/P2N1PPP/R2QK2R w KQkq a6 0 14",
		"4rrk1/2p1b1p1/p1p3q1/4p3/2P2n1p/1P1NR2P/PB3PP1/3R1QK1 b - - 2 24",
		"r3qbrk/6p1/2b2pPp/p3pP1Q/PpPpP2P/3P1B2/2PB3K/R5R1 w - - 16 42",
		"6k1/1R3p2/6p1/2Bp3p/3P2q1/P7/1P2rQ1K/5R2 b - - 4 44",
		"8/8/1p2k1p1/3p3p/1p1P1P1P/1P2PK2/8/8 w - - 3 54",
		"7r/2p3k1/1p1p1qp1/1P1Bp3/p1P2r1P/P7/4R3/Q4RK1 w - - 0 36",
		"r1bq1rk1/pp2b1pp/n1pp1n2/3P1p2/2P1p3/2N1P2N/PP2BPPP/R1BQ1RK1 b - - 2 10",
		"3r3k/2r4p/1p1b3q/p4P2/P2Pp3/1B2P3/3BQ1RP/6K1 w - - 3 87",
		"2r4r/1p4k1/1Pnp4/3Qb1pq/8/4BpPp/5P2/2RR1BK1 w - - 0 42",
		"4q1bk/6b1/7p/p1p4p/PNPpP2P/KN4P1/3Q4/4R3 b - - 0 37",
		"2q3r1/1r2pk2/pp3pp1/2pP3p/P1Pb1BbP/1P4Q1/R3NPP1/4R1K1 w - - 2 34",
		"1r2r2k/1b4q1/pp5p/2pPp1p1/P3Pn2/1P1B1Q1P/2R3P1/4BR1K b - - 1 37",
		"r3kbbr/pp1n1p1P/3ppnp1/q5N1/1P1pP3/P1N1B3/2P1QP2/R3KB1R b KQkq b3 0 17",
		"8/6pk/2b1Rp2/3r4/1R1B2PP/P5K1/8/2r5 b - - 16 42",
		"1r4k1/4ppb1/2n1b1qp/pB4p1/1n1BP1P1/7P/2PNQPK1/3RN3 w - - 8 29",
		"8/p2B4/PkP5/4p1pK/4Pb1p/5P2/8/8 w - - 29 68",
		"3r4/ppq1ppkp/4bnp1/2pN4/2P1P3/1P4P1/PQ3PBP/R4K2 b - - 2 20",
		"5rr1/4n2k/4q2P/P1P2n2/3B1p2/4pP2/2N1P3/1RR1K2Q w - - 1 49",
		"1r5k/2pq2p1/3p3p/p1pP4/4QP2/PP1R3P/6PK/8 w - - 1 51",
		"q5k1/5ppp/1r3bn1/1B6/P1N2P2/BQ2P1P1/5K1P/8 b - - 2 34",
		"r1b2k1r/5n2/p4q2/1ppn1Pp1/3pp1p1/NP2P3/P1PPBK2/1RQN2R1 w - - 0 22",
		"r1bqk2r/pppp1ppp/5n2/4b3/4P3/P1N5/1PP2PPP/R1BQKB1R w KQkq - 0 5",
		"r1bqr1k1/pp1p1ppp/2p5/8/3N1Q2/P2BB3/1PP2PPP/R3K2n b Q - 1 12",
		"r1bq2k1/p4r1p/1pp2pp1/3p4/1P1B3Q/P2B1N2/2P3PP/4R1K1 b - - 2 19",
		"r4qk1/6r1/1p4p1/2ppBbN1/1p5Q/P7/2P3PP/5RK1 w - - 2 25",
		"r7/6k1/1p6/2pp1p2/7Q/8/p1P2K1P/8 w - - 0 32",
		"r3k2r/ppp1pp1p/2nqb1pn/3p4/4P3/2PP4/PP1NBPPP/R2QK1NR w KQkq - 1 5",
		"3r1rk1/1pp1pn1p/p1n1q1p1/3p4/Q3P3/2P5/PP1NBPPP/4RRK1 w - - 0 12",
		"5rk1/1pp1pn1p/p3Brp1/8/1n6/5N2/PP3PPP/2R2RK1 w - - 2 20",
		"8/1p2pk1p/p1p1r1p1/3n4/8/5R2/PP3PPP/4R1K1 b - - 3 27",
		"8/4pk2/1p1r2p1/p1p4p/Pn5P/3R4/1P3PP1/4RK2 w - - 1 33",
		"8/5k2/1pnrp1p1/p1p4p/P6P/4R1PK/1P3P2/4R3 b - - 1 38",
		"8/8/1p1kp1p1/p1pr1n1p/P6P/1R4P1/1P3PK1/1R6 b - - 15 45",
		"8/8/1p1k2p1/p1prp2p/P2n3P/6P1/1P1R1PK1/4R3 b - - 5 49",
		"8/8/1p4p1/p1p2k1p/P2npP1P/4K1P1/1P6/3R4 w - - 6 54",
		"8/8/1p4p1/p1p2k1p/P2n1P1P/4K1P1/1P6/6R1 b - - 6 59",
		"8/5k2/1p4p1/p1pK3p/P2n1P1P/6P1/1P6/4R3 b - - 14 63",
		"8/1R6/1p1K1kp1/p6p/P1p2P1P/6P1/1Pn5/8 w - - 0 67",
		"1rb1rn1k/p3q1bp/2p3p1/2p1p3/2P1P2N/PP1RQNP1/1B3P2/4R1K1 b - - 4 23",
		"4rrk1/pp1n1pp1/q5p1/P1pP4/2n3P1/7P/1P3PB1/R1BQ1RK1 w - - 3 22",
		"r2qr1k1/pb1nbppp/1pn1p3/2ppP3/3P4/2PB1NN1/PP3PPP/R1BQR1K1 w - - 4 12",
		"2r2k2/8/4P1R1/1p6/8/P4K1N/7b/2B5 b - - 0 55",
		"6k1/5pp1/8/2bKP2P/2P5/p4PNb/B7/8 b - - 1 44",
		"2rqr1k1/1p3p1p/p2p2p1/P1nPb3/2B1P3/5P2/1PQ2NPP/R1R4K w - - 3 25",
		"r1b2rk1/p1q1ppbp/6p1/2Q5/8/4BP2/PPP3PP/2KR1B1R b - - 2 14",
		"6r1/5k2/p1b1r2p/1pB1p1p1/1Pp3PP/2P1R1K1/2P2P2/3R4 w - - 1 36",
		"rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq c3 0 2",
		"2rr2k1/1p4bp/p1q1p1p1/4Pp1n/2PB4/1PN3P1/P3Q2P/2RR2K1 w - f6 0 20",
		"3br1k1/p1pn3p/1p3n2/5pNq/2P1p3/1PN3PP/P2Q1PB1/4R1K1 w - - 0 23",
		"2r2b2/5p2/5k2/p1r1pP2/P2pB3/1P3P2/K1P3R1/7R w - - 23 93",
		"8/pp2nkR1/5n1p/3p4/5p2/P2BP3/1PPKN3/8 b - - 0 31",
		"3n4/2k3p1/p4r2/1pp4P/5PB1/P6P/1KP5/5R2 w - - 0 32",
		"4r1k1/5p1p/6pP/2b5/1p3R2/pP2BKP1/P4P2/8 b - - 0 38"
	};

	// The bench search over the standard position set. This is the whole engine end to end -
	// move generation, search, the incremental accumulator and the fused update kernels - so
	// a wandering node count here is the broadest signal that something has gone wrong.
	TEST_CLASS( ClarityBenchTest )
	{
	public:
		// kept lower than the engine's own default of 14 so the suite stays quick.
		static constexpr int32_t benchDepth = 8;

		// the depth Clarity.exe bench used, and therefore the one OpenBench reports on.
		// Tests at this depth are tagged Slow and are meant to be filtered out of routine
		// runs with /TestCaseFilter:"Name!~Slow".
		static constexpr int32_t engineBenchDepth = 14;

		// The bench signature at engineBenchDepth. This is expected to change whenever search
		// or evaluation changes on purpose - update it deliberately, and treat an unplanned
		// change as a behaviour regression worth investigating.
		static constexpr uint64_t referenceBenchNodes = 5294115;

		struct BenchResult
		{
			uint64_t nodes;
			int64_t elapsedMilliseconds;
		};

		static BenchResult runBench( const int32_t depth )
		{
			// Engine embeds SearchInfo by value, whose history tables run to tens of
			// megabytes, so it must live on the heap - a stack instance overflows and takes
			// the whole test host with it.
			TranspositionTable transposition_table;
			std::unique_ptr<Engine> engine = std::make_unique<Engine>( &transposition_table );

			activeBenchEngine = engine.get();
			engine->resetEngine();

			SearchLimiters limit = {};
			limit.writeValues( 0, 0, 20, depth, 0, 0 );

			uint64_t total = 0;
			const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

			for( const std::string& fen : benchFens )
			{
				Board bench_board( fen );
				std::array<Move, 256> moves;
				const int32_t move_count = bench_board.getMoves( moves );

				engine->think( bench_board, limit, false, moves, move_count );
				total += engine->nodes;
			}

			const int64_t elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - begin ).count();

			activeBenchEngine = nullptr;

			return BenchResult{ total, elapsed_milliseconds };
		}

		TEST_METHOD_CATEGORY( BenchSearchesEveryPosition, Bench )
		{
			const BenchResult result = runBench( benchDepth );

			const int64_t npms = result.nodes / result.elapsedMilliseconds;
			Logger::WriteMessage( ( std::to_string( result.nodes ) + " nodes in " + std::to_string( result.elapsedMilliseconds ) + " ms at depth " + std::to_string( benchDepth ) + " (" + std::to_string( npms ) + " npms)\n" ).c_str() );

			Assert::IsTrue( result.nodes > 0, L"The bench must visit nodes" );
			Assert::IsTrue( !benchFens.empty(), L"The bench position set must not be empty" );
		}

		TEST_METHOD_CATEGORY( BenchIsDeterministic, Bench )
		{
			// each run gets its own transposition table, so a repeat must land on exactly the
			// same node count. Uninitialised state or a stale accumulator shows up here.
			const BenchResult first = runBench( benchDepth );
			const BenchResult second = runBench( benchDepth );

			Assert::AreEqual( first.nodes, second.nodes, L"Two bench runs from a clean transposition table must search identically" );
		}

		TEST_METHOD_CATEGORY( SlowBenchAtEngineDepthMatchesReference, Slow )
		{
			const BenchResult result = runBench( engineBenchDepth );

			const int64_t npms = result.nodes / result.elapsedMilliseconds;
			Logger::WriteMessage( ( std::to_string( result.nodes ) + " nodes in " + std::to_string( result.elapsedMilliseconds ) + " ms at depth " + std::to_string( engineBenchDepth ) + " (" + std::to_string( npms ) + " npms)\n" ).c_str() );

			Assert::AreEqual( referenceBenchNodes, result.nodes, L"Bench signature changed - update referenceBenchNodes if this was intended" );
		}
	};

	// The Ethereal perft suite, formerly the engine's own "perftest" command. 755 positions
	// totalling roughly 4.8 billion nodes, so the whole thing runs for about fourteen minutes -
	// every batch is named Slow and is excluded from routine runs by /TestCaseFilter:"Name!~Slow".
	TEST_CLASS( ClarityPerftTest )
	{
	public:
		// 76 x 10 covers all 755 entries, with the last batch slightly short.
		static constexpr size_t perftBatchSize = 76;

		// perft lives in the engine's tests.cpp, which belongs to Clarity.exe rather than
		// ClarityCommon, so the suite needs its own copy. UpdateNNUE is false because counting
		// nodes never evaluates, which keeps the accumulator entirely out of the measurement.
		static int32_t perft( Board& board, const int32_t depth )
		{
			if( depth == 0 )
			{
				return 1;
			}

			std::array<Move, 256> moves;
			const int32_t move_count = board.getMoves( moves );
			int32_t result = 0;

			for( int32_t i = 0; i < move_count; i++ )
			{
				if( board.makeMove<false, false>( moves[i] ) )
				{
					result += perft( board, depth - 1 );
					board.undoMove<false, false>();
				}
			}

			return result;
		}

		static void runPerftBatch( const size_t batchIndex )
		{
			const size_t first = batchIndex * perftBatchSize;
			Assert::IsTrue( first < etherealSuite.size(), L"Perft batch starts past the end of the suite" );

			const size_t last = std::min( first + perftBatchSize, etherealSuite.size() );
			int64_t total = 0;
			const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

			for( size_t i = first; i < last; i++ )
			{
				const PerftTest& test = etherealSuite[i];
				Board board( test.fen );
				const int32_t result = perft( board, test.depth );

				total += result;

				if( result != test.expectedOutput )
				{
					const std::string message = "perft mismatch at suite entry " + std::to_string( i )
						+ ": fen " + test.fen
						+ " depth " + std::to_string( test.depth )
						+ " expected " + std::to_string( test.expectedOutput )
						+ " got " + std::to_string( result );

					Assert::Fail( std::wstring( message.begin(), message.end() ).c_str() );
				}
			}

			const int64_t elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - begin ).count();

			const int64_t npms = total / elapsed_milliseconds;
			Logger::WriteMessage( ( "entries " + std::to_string( first ) + "-" + std::to_string( last - 1 )
				+ ": " + std::to_string( total ) + " nodes in " + std::to_string( elapsed_milliseconds ) + " ms ("+ std::to_string( npms ) +" npms)\n" ).c_str() );
		}

		TEST_METHOD_CATEGORY( SlowPerftSuiteBatch0, Perft ) { runPerftBatch( 0 ); }
		TEST_METHOD_CATEGORY( SlowPerftSuiteBatch1, Perft ) { runPerftBatch( 1 ); }
		TEST_METHOD_CATEGORY( SlowPerftSuiteBatch2, Perft ) { runPerftBatch( 2 ); }
		TEST_METHOD_CATEGORY( SlowPerftSuiteBatch3, Perft ) { runPerftBatch( 3 ); }
		TEST_METHOD_CATEGORY( SlowPerftSuiteBatch4, Perft ) { runPerftBatch( 4 ); }
		TEST_METHOD_CATEGORY( SlowPerftSuiteBatch5, Perft ) { runPerftBatch( 5 ); }
		TEST_METHOD_CATEGORY( SlowPerftSuiteBatch6, Perft ) { runPerftBatch( 6 ); }
		TEST_METHOD_CATEGORY( SlowPerftSuiteBatch7, Perft ) { runPerftBatch( 7 ); }
		TEST_METHOD_CATEGORY( SlowPerftSuiteBatch8, Perft ) { runPerftBatch( 8 ); }
		TEST_METHOD_CATEGORY( SlowPerftSuiteBatch9, Perft ) { runPerftBatch( 9 ); }
	};
}
