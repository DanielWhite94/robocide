CC = gcc
CFLAGS = -pthread -lm -Wall -O3 -flto -Wno-unused-local-typedefs -DNDEBUG #-DTUNE

OBJS = src/attacks.o src/bb.o src/bitbase.o src/colour.o src/eval.o src/fen.o \
       src/history.o src/htable.o src/magicmoves.o src/move.o src/moves.o \
       src/perft.o src/piece.o src/pos.o src/score.o src/scoredmove.o \
       src/search.o src/see.o src/square.o src/thread.o src/time.o src/tt.o \
       src/uci.o src/util.o

MAIN = src/main.c

NAME = robocide

all: $(OBJS)
	$(CC) $(MAIN) $(OBJS) -o $(NAME) $(CFLAGS)
$(OBJS):

clean:
	@rm -f $(OBJS)
