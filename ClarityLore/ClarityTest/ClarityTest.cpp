// Copyright Eternal Developments, LLC. All rights reserved.

#include <filesystem>
#include <string>

// windows.h defines min/max macros, which collide with Tunable::max in tunables.h.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "../src/globals.h"
#include "../src/see.h"
#include "../src/tunables.h"

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

	// the colour half of a feature block: six piece types of sixty four squares.
	constexpr int32_t ColorStride = 64 * 6;

	// squares are numbered a1 = 0 through h8 = 63.
	constexpr int32_t squareIndex( const int32_t file, const int32_t rank )
	{
		return ( rank * 8 ) + file;
	}

	constexpr int32_t squareD4 = squareIndex( 3, 3 );
	constexpr int32_t squareD5 = squareIndex( 3, 4 );
	constexpr int32_t squareE1 = squareIndex( 4, 0 );
	constexpr int32_t squareE8 = squareIndex( 4, 7 );

	const std::string startPositionWhiteToMove = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
	const std::string startPositionBlackToMove = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1";
	const std::string whiteQueenOnD4 = "4k3/8/8/8/3Q4/8/8/4K3 w - - 0 1";
	const std::string blackQueenOnD4 = "4k3/8/8/8/3q4/8/8/4K3 w - - 0 1";
	const std::string emptyBoard = "4k3/8/8/8/8/8/8/4K3 w - - 0 1";

	// anchor used only to find the module this code was linked into.
	static void moduleAnchor()
	{
	}

	// The engine loads Data/cn_031.nnue relative to the working directory, which is not
	// where the test host starts. Walk up from the test binary until the file is found.
	static bool setWorkingDirectoryToRepositoryRoot()
	{
		HMODULE module_handle = nullptr;
		if( GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
								reinterpret_cast<LPCWSTR>( &moduleAnchor ), &module_handle ) == 0 )
		{
			return false;
		}

		wchar_t module_path[MAX_PATH] = {};
		if( GetModuleFileNameW( module_handle, module_path, MAX_PATH ) == 0 )
		{
			return false;
		}

		std::filesystem::path candidate = std::filesystem::path( module_path ).parent_path();
		for( int32_t depth = 0; depth < 8; depth++ )
		{
			std::error_code error_code;
			if( std::filesystem::exists( candidate / "Data" / "cn_031.nnue", error_code ) )
			{
				std::filesystem::current_path( candidate, error_code );
				return !error_code;
			}

			if( !candidate.has_parent_path() || candidate.parent_path() == candidate )
			{
				break;
			}

			candidate = candidate.parent_path();
		}

		return false;
	}

	TEST_MODULE_INITIALIZE( ClarityTestModuleInitialize )
	{
		Assert::IsTrue( setWorkingDirectoryToRepositoryRoot(), L"Could not locate Data/cn_031.nnue - the evaluation tests cannot run without the network" );

		initialize();
	}

	// Regression cover for the empty-square sentinel. EPieceType was renumbered so that
	// None = 0, but the mailbox kept storing PieceTypeCount (7) for an empty square. Every
	// value table is sized PieceTypeCount, so a quiet move indexed one past the end of
	// SEE_values and dereferenced a garbage Tunable pointer.
	TEST_CLASS( ClarityPieceEncodingTest )
	{
	public:
		TEST_METHOD_CATEGORY( EmptySquareSentinelIsNone, PieceEncoding )
		{
			Board board( emptyBoard );

			Assert::AreEqual( static_cast<int8_t>( EPiece::None ), static_cast<int8_t>( board.pieceAtIndex( squareD4 ) ), L"An empty square must read back as EPiece::None" );
		}

		TEST_METHOD_CATEGORY( EmptySquareIndexesValueTablesInRange, PieceEncoding )
		{
			const int32_t empty_type = getType( EPiece::None );

			Assert::IsTrue( empty_type < static_cast<int32_t>( SEE_values.size() ), L"getType of an empty square must be a valid SEE_values index" );
			Assert::IsTrue( empty_type < static_cast<int32_t>( MVV_values.size() ), L"getType of an empty square must be a valid MVV_values index" );
		}

		TEST_METHOD_CATEGORY( EveryOccupiedSquareIndexesValueTablesInRange, PieceEncoding )
		{
			const std::string positions[] = { startPositionWhiteToMove, whiteQueenOnD4, blackQueenOnD4, emptyBoard };

			for( const std::string& fen : positions )
			{
				Board board( fen );

				for( int8_t square = 0; square < 64; square++ )
				{
					const int32_t piece_type = getType( board.pieceAtIndex( square ) );

					Assert::IsTrue( piece_type >= 0 && piece_type < static_cast<int32_t>( SEE_values.size() ),
									L"Every square must produce an in-range SEE_values index" );
					Assert::IsTrue( piece_type < static_cast<int32_t>( BoardState().pieceBitboards.size() ),
									L"Every square must produce an in-range pieceBitboards index" );
				}
			}
		}

		TEST_METHOD_CATEGORY( PieceColourIsEncodedInBitThree, PieceEncoding )
		{
			Assert::IsTrue( getColor( EPiece::WhitePawn ) == EColor::White, L"Pawn | White must read as WHITE" );
			Assert::IsTrue( getColor( EPiece::BlackPawn ) == EColor::Black, L"Pawn | Black must read as BLACK" );
			Assert::IsTrue( getType( EPiece::WhiteQueen ) == EPieceType::Queen, L"The colour bit must not disturb the type" );
			Assert::IsTrue( getType( EPiece::BlackQueen ) == EPieceType::Queen, L"The colour bit must not disturb the type" );
		}

		TEST_METHOD_CATEGORY( ValueTablesCoverEveryPieceType, PieceEncoding )
		{
			Assert::AreEqual( static_cast<size_t>( EPieceType::PieceTypeCount ), SEE_values.size(), L"SEE_values must have an entry for every piece type including None" );
			Assert::AreEqual( static_cast<size_t>( EPieceType::PieceTypeCount ), MVV_values.size(), L"MVV_values must have an entry for every piece type including None" );
		}
	};

	// Regression cover for see.h. The promotions table is indexed by EPieceType, but see.h
	// still scanned promotions[0..3]. Entries 0 and 1 hold 0b0000, which is the Normal flag,
	// so every ordinary capture matched as a promotion and was valued at zero.
	TEST_CLASS( ClaritySeeTest )
	{
	public:
		TEST_METHOD_CATEGORY( PromotionFlagsDoNotCollideWithNormal, See )
		{
			for( int32_t promoted = EPieceType::Knight; promoted <= EPieceType::Queen; promoted++ )
			{
				Assert::AreNotEqual( static_cast<int32_t>( Normal ), static_cast<int32_t>( promotions[promoted] ),
									 L"A promotion flag must never equal the Normal flag" );
				Assert::IsTrue( promotions[promoted] > DoublePawnPush,
								L"Promotion flags are expected to be the highest flag values" );
			}
		}

		TEST_METHOD_CATEGORY( PromotionFlagsAreDistinctPerPieceType, See )
		{
			for( int32_t left = EPieceType::Knight; left <= EPieceType::Queen; left++ )
			{
				for( int32_t right = left + 1; right <= EPieceType::Queen; right++ )
				{
					Assert::AreNotEqual( static_cast<int32_t>( promotions[left] ), static_cast<int32_t>( promotions[right] ),
										 L"Each promotion piece type must have its own flag" );
				}
			}
		}

		TEST_METHOD_CATEGORY( CaptureIsValuedAtTheVictim, See )
		{
			Board board( whiteQueenOnD4 );

			const int32_t estimated = estimateMoveValue( board, squareD4, Normal );
			const int32_t expected = static_cast<int32_t>( SEE_values[EPieceType::Queen]->value );

			Assert::AreEqual( expected, estimated, L"Capturing a queen must be valued as a queen, not zero" );
			Assert::IsTrue( estimated > 0, L"A capture must have a positive estimated value" );
		}

		TEST_METHOD_CATEGORY( QuietMoveIsValuedAtZero, See )
		{
			Board board( whiteQueenOnD4 );

			const int32_t estimated = estimateMoveValue( board, squareD5, Normal );
			const int32_t expected = static_cast<int32_t>( SEE_values[EPiece::None]->value );

			Assert::AreEqual( expected, estimated, L"A move to an empty square captures nothing" );
		}

		TEST_METHOD_CATEGORY( PromotionIsValuedAtThePromotedPiece, See )
		{
			Board board( whiteQueenOnD4 );

			const int32_t estimated = estimateMoveValue( board, squareD5, promotions[EPieceType::Queen] );
			const int32_t expected = static_cast<int32_t>( SEE_values[EPieceType::Queen]->value - SEE_values[EPieceType::Pawn]->value );

			Assert::AreEqual( expected, estimated, L"A queen promotion gains a queen and loses a pawn" );
		}

		TEST_METHOD_CATEGORY( KnightPromotionIsValuedAsAKnight, See )
		{
			Board board( whiteQueenOnD4 );

			const int32_t estimated = estimateMoveValue( board, squareD5, promotions[EPieceType::Knight] );
			const int32_t expected = static_cast<int32_t>( SEE_values[EPieceType::Knight]->value - SEE_values[EPieceType::Pawn]->value );

			Assert::AreEqual( expected, estimated, L"Promotion value must follow the promoted piece type" );
		}
	};

	// Regression cover for the NNUE feature index. The colour half of the input block was
	// selected by the accumulator perspective rather than by the piece, so a white queen and
	// a black queen on the same square produced the same feature and the evaluation could
	// not tell the two apart.
	TEST_CLASS( ClarityFeatureIndexTest )
	{
	public:
		TEST_METHOD_CATEGORY( WhitePerspectiveSeparatesPieceColour, FeatureIndex )
		{
			const int32_t white_piece = NetworkState::getWhiteFeatureIndex( squareD4, EPieceType::Queen, 0, squareE1 );
			const int32_t black_piece = NetworkState::getWhiteFeatureIndex( squareD4, EPieceType::Queen, ColorStride, squareE1 );

			Assert::AreNotEqual( white_piece, black_piece,
								 L"The white perspective must distinguish a white queen from a black queen" );
			Assert::AreEqual( ColorStride, black_piece - white_piece,
							  L"Opposing pieces must sit exactly one colour block apart" );
		}

		TEST_METHOD_CATEGORY( BlackPerspectiveSeparatesPieceColour, FeatureIndex )
		{
			const int32_t white_piece = NetworkState::getBlackFeatureIndex( squareD4, EPieceType::Queen, ColorStride, squareE8 );
			const int32_t black_piece = NetworkState::getBlackFeatureIndex( squareD4, EPieceType::Queen, 0, squareE8 );

			Assert::AreNotEqual( white_piece, black_piece,
								 L"The black perspective must distinguish a white queen from a black queen" );
			Assert::AreEqual( ColorStride, white_piece - black_piece,
							  L"Opposing pieces must sit exactly one colour block apart" );
		}

		TEST_METHOD_CATEGORY( OwnPiecesShareTheFirstColourBlock, FeatureIndex )
		{
			// the network is trained with the side to move first, so from either perspective
			// our own pieces occupy the low half of the block and theirs the high half.
			const int32_t white_from_white = NetworkState::getWhiteFeatureIndex( squareD4, EPieceType::Queen, 0, squareE1 );
			const int32_t black_from_black = NetworkState::getBlackFeatureIndex( squareD4, EPieceType::Queen, 0, squareE1 );

			Assert::IsTrue( ( white_from_white % inputSize ) < ColorStride,
							L"Our own piece must land in the first colour block from the white perspective" );
			Assert::IsTrue( ( black_from_black % inputSize ) < ColorStride,
							L"Our own piece must land in the first colour block from the black perspective" );
		}

		TEST_METHOD_CATEGORY( PieceTypesAreDistinctWithinAColourBlock, FeatureIndex )
		{
			const int32_t pawn = NetworkState::getWhiteFeatureIndex( squareD4, EPieceType::Pawn, 0, squareE1 );
			const int32_t queen = NetworkState::getWhiteFeatureIndex( squareD4, EPieceType::Queen, 0, squareE1 );

			Assert::AreNotEqual( pawn, queen, L"Different piece types must produce different features" );
		}

		TEST_METHOD_CATEGORY( EveryFeatureIndexIsWithinTheWeightTable, FeatureIndex )
		{
			constexpr int32_t feature_count = inputSize * inputBucketCount;

			for( int32_t king = 0; king < 64; king++ )
			{
				for( int32_t square = 0; square < 64; square++ )
				{
					for( EPieceType pieceType : { EPieceType::Pawn, EPieceType::Knight, EPieceType::Bishop, EPieceType::Rook, EPieceType::Queen, EPieceType::King } )
					{
						const int32_t white_index = NetworkState::getWhiteFeatureIndex( square, pieceType, 0, king );
						const int32_t black_index = NetworkState::getBlackFeatureIndex( square, pieceType, 0, king );

						Assert::IsTrue( white_index >= 0 && white_index < feature_count, L"White perspective feature index out of range" );
						Assert::IsTrue( black_index >= 0 && black_index < feature_count, L"Black perspective feature index out of range" );
					}
				}
			}
		}
	};

	// Regression cover for refreshAccumulator, which applied its updates to the search stack
	// and then copied the untouched refresh table entry over the top. The accumulator ended
	// up holding nothing but the feature biases, so every position evaluated to roughly the
	// same number regardless of the pieces on the board.
	TEST_CLASS( ClarityEvaluationTest )
	{
	public:
		TEST_METHOD_CATEGORY( StartPositionIsSymmetric, Evaluation )
		{
			Board white_to_move( startPositionWhiteToMove );
			Board black_to_move( startPositionBlackToMove );

			Assert::AreEqual( white_to_move.getEvaluation(), black_to_move.getEvaluation(),
							  L"The start position must evaluate the same for either side to move" );
		}

		TEST_METHOD_CATEGORY( AnExtraQueenIsWinning, Evaluation )
		{
			Board board( whiteQueenOnD4 );

			Assert::IsTrue( board.getEvaluation() > 500,
							L"Being a queen up must evaluate strongly in favour of the side to move" );
		}

		TEST_METHOD_CATEGORY( AMissingQueenIsLosing, Evaluation )
		{
			Board board( blackQueenOnD4 );

			Assert::IsTrue( board.getEvaluation() < -500,
							L"Being a queen down must evaluate strongly against the side to move" );
		}

		TEST_METHOD_CATEGORY( SwappingTheQueenColourMirrorsTheEvaluation, Evaluation )
		{
			Board white_queen( whiteQueenOnD4 );
			Board black_queen( blackQueenOnD4 );

			// the two positions are mirror images in material, so the evaluations should be
			// near opposites - king placement keeps this from being exact.
			const int32_t sum = white_queen.getEvaluation() + black_queen.getEvaluation();

			Assert::IsTrue( std::abs( sum ) < 250,
							L"A white queen and a black queen must be worth the same to their owner" );
		}

		TEST_METHOD_CATEGORY( PieceColourChangesTheEvaluation, Evaluation )
		{
			Board white_queen( whiteQueenOnD4 );
			Board black_queen( blackQueenOnD4 );

			const int32_t white_evaluation = white_queen.getEvaluation();
			const int32_t black_evaluation = black_queen.getEvaluation();

			Assert::AreNotEqual( white_evaluation, black_evaluation,
								 L"Swapping the colour of the only piece must change the evaluation" );
			Assert::IsTrue( white_evaluation > 0 && black_evaluation < 0,
							L"The extra queen must favour whichever side owns it" );
		}

		TEST_METHOD_CATEGORY( MaterialIsReflectedInTheAccumulator, Evaluation )
		{
			Board empty( emptyBoard );
			Board with_queen( whiteQueenOnD4 );

			Assert::AreNotEqual( empty.getEvaluation(), with_queen.getEvaluation(),
								 L"Adding a queen must change the evaluation - a blank accumulator would not" );
		}
	};
}
