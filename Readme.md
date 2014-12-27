### Overview

Robocide is a free, open-source UCI chess engine written in C from scratch. It
is not a complete chess playing program and requires an interface supporting the
UCI protocol.

In addition to the usual features of modern chess engines such as a
transposition table and null-move-pruning, Robocide also tries to implement much
special case knowledge. An example of this can be seen in blocked/fortress
positions such as 3B4/1r2p3/r2p1p2/bkp1P1p1/1p1P1PPp/p1P1K2P/PPB5/8 w - -.
Another non-standard feature is the ability to compile a 'tuning' version, where
many of the search and evaluation parameters can be adjusted, see the section on
UCI options for more information.

SMP (threaded search) is not currently supported, although it is on the TODO
list.

### Files

This distribution contains the following files:
* Readme.md - this file.
* Copying.txt - the GNU General Public License under which Robocide is released.
* src - Full source code and makefile.

### UCI Options

By default, the following options are available:
* PawnHash - Size of the pawn hash table in megabytes. While a larger number
should result in more hits, the memory is probably better used on the standard
hash table (as the pawn table already enjoys a high hit-rate).
* MatHash - Similar to the PawnHash option but for the material combination
table.
* Hash - The size of the main transposition table in megabytes. Generally, a
larger value will result in better play, especially in longer games.
* Ponder - Turn pondering on/off. Note that as per the UCI specification,
Robocide will not start pondering automatically, instead requiring the
GUI/interface to send 'go ponder'.

Furthermore, if tuning is enabled (see the section on compiling) many more
options are available:
* [PIECE][MG/EG] - The material value of PIECE.
* PawnCentre[MG/EG] - Bonus for each pawn on D4, D5, E4 or E5.
* PawnOuterCentre[MG/EG] - Bonus for each pawn on C3-C6, D3-D6, E3-E6 or F3-F6.
* PawnFiles[FILE][MG/EG] - Bonus for each pawn on the given file, FILE.
* PawnRank[RANK][MG/EG] - Bonus for each pawn on the given rank, RANK.
* PawnDoubled[MG/EG] - Bonus for each doubled pawn.
* PawnIsolated[MG/EG] - Bonus for each isolated pawn.
* PawnBlocked[MG/EG] - Bonus for each blocked pawn.
* PawnPassedQuad[COEFFICIENT][MG/EG] - Specifies the quadratic coefficients for
the bonus applied to a passed pawns (the bonus is given by a*r^2+b*r+c, for a
pawn on rank r).
* [PIECE]PawnAffinity[MG/EG] - Bonus a given piece PIECE receives for each
friendly pawn.
* BishopPair[MG/EG] - Bonus for having at least one bishop of each colour.
* BishopMobility[MG/EG] - Bonus for each square a bishop attacks.
* OppositeBishopFactor[MG/EG] - Factor to reduce the score by if we are in an
opposite bishop situation. A value of 256 represents no change, 128 would half
the score.
* RookMobilityFile[MG/EG] - Bonus for each square a rook attacks along a file.
* RookMobilityRank[MG/EG] - Bonus for each square a rook attacks along a rank.
* RookOpenFile[MG/EG] - Bonus for each rook on an open file (a file without any
pawns).
* RookSemiOpenFile[MG/EG] - Bonus for each rook on a semi-open file (a file with
opponent pawns but without any friendly pawns).
* RookOn7th[MG/EG] - Bonus for having a rook on the 7th rank (does not always
apply, see eval.c for full details).
* RookTrapped[MG/EG] - Bonus for rook trapped in by pawns and own king.
* KingShieldClose[MG/EG] - Bonus for each pawn one rank infront of the king, and
at most one file away.
* KingShieldFar[MG/EG] - Bonus for each pawn two ranks infront of the king, and
at most one file away.
* Tempo[COMBINATION][MG/EG] - Bonus for having the side to move in the given
material combination.
* HalfMoveFactor - How severely to drag score towards 0 as we approach the
50-move rule. A lower value will drag the score towards 0 sooner.
* WeightFactor - Determines how we interpolate the score between middlegame and
endgame values. A lower value will cause the endgame score to be considered more
important for the same material combination.
* NullReduction - How much to reduce the search depth by in the case of
performing null-move-pruning.
* IIDMin - Minimum depth when entering a node at which internal iterative
deepening is considered.
* IIDReduction - How much to reduce the search depth by in the case of
performing internal iterative deepening.

### Compiling

On Unix-like systems, running 'make' in the src/ directory should be sufficient.
It is also possible to produce a 'tuning' version by running 'make tune'. See
the section on UCI options for more information. It is wise to run 'make clean'
before each 'make' call, to ensure all object files are up to date, especially
if changing from the standard to the tuning version, or vice-versa.

Windows is not currently supported, although hopefully this will change in the
near future.

### Acknowledgements

I would like to thank the Computer Chess Club and the Chess Programming Wiki for
providing inspiration and indeed many of the ideas used. In addition, the
bitboard magic move generator written by Pradyumna Kannan was hugely helpful in
avoiding having to reinvent the wheel and proceeding quickly to the more
interesting parts of chess engine development (see magicmoves.c/h).

### Terms of use

Robocide is free, and distributed under the **GNU General Public License**
(GPL). Essentially, this means that you are free to do almost exactly
what you want with the program, including distributing it among your
friends, making it available for download from your web site, selling
it (either by itself or as part of some bigger software package), or
using it as the starting point for a software project of your own.

The only real limitation is that whenever you distribute Robocide in
some way, you must always include the full source code, or a pointer
to where the source code can be found. If you make any changes to the
source code, these changes must also be made available under the GPL.

For full details, read the copy of the GPL found in the file named
*Copying.txt*.
