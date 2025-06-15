### Overview

Robocide is a free, open-source UCI chess engine written in C from scratch. It
is not a complete chess playing program and requires an interface supporting the
UCI protocol.

In addition to the usual features of modern chess engines such as a
transposition table and null-move-pruning, Robocide also tries to implement much
special case knowledge. An example of this can be seen in blocked/fortress
positions such as `3B4/1r2p3/r2p1p2/bkp1P1p1/1p1P1PPp/p1P1K2P/PPB5/8 w - -`.
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
* UCI_Chess960 - standard UCI option to enable/disable Chess960 mode (changes representation and logic of castling rights and moves)
* MatHash - Similar to the PawnHash option but for the material combination
table.
* Hash - The size of the main transposition table in megabytes. Generally, a
larger value will result in better play, especially in longer games.
* Ponder - Turn pondering on/off. Note that as per the UCI specification,
Robocide will not start pondering automatically, instead requiring the
GUI/interface to send 'go ponder'.

Furthermore, if tuning is enabled (see the section on compiling) many more
options are available (run the engine and type 'uci' to see a list of current parameters).

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
