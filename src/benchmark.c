#include "benchmark.h"
#include "depth.h"
#include "eval.h"
#include "search.h"

////////////////////////////////////////////////////////////////////////////////
// Private prototypes.
////////////////////////////////////////////////////////////////////////////////

unsigned long long int benchmarkFen(const char *fen, Depth depth);

////////////////////////////////////////////////////////////////////////////////
// Public functions.
////////////////////////////////////////////////////////////////////////////////

unsigned long long int benchmark(void) {
	searchClear();
	evalClear();

	unsigned long long int nodes=0;
	nodes+=benchmarkFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 8);
	nodes+=benchmarkFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 7);
	nodes+=benchmarkFen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 10);
	nodes+=benchmarkFen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 8);
	nodes+=benchmarkFen("rnbqkb1r/pp1p1ppp/2p5/4P3/2B5/8/PPP1NnPP/RNBQK2R w KQkq - 0 6", 7);
	nodes+=benchmarkFen("r1bq1rk1/pppnnppp/4p3/3pP3/1b1P4/2NB3N/PPP2PPP/R1BQK2R w KQ - 3 7", 7);
	nodes+=benchmarkFen("rnb3nr/ppq2kpp/4pp2/1B1pP3/P5Q1/B1p2N2/2P2PPP/R4RK1 w - - 0 1", 6);
	nodes+=benchmarkFen("3B4/1r2p3/r2p1p2/bkp1P1p1/1p1P1PPp/p1P1K2P/PPB5/8 w - - 0 1", 9);

	return nodes;
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

unsigned long long int benchmarkFen(const char *fen, Depth depth) {
	Pos *pos=posNew(fen);
	if (pos==NULL)
		return 0;

	unsigned long long int nodes=searchBenchmark(pos, depth);

	posFree(pos);

	return nodes;
}
