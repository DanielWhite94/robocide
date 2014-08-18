CC = gcc
CFLAGS = -pthread -lm -Wall -O3 -flto -DNDEBUG #-DTUNE

OBJS = src/attacks.o src/bb.o src/eval.o src/fen.o src/htable.o src/magicmoves.o \
       src/moves.o src/pos.o src/perft.o src/search.o src/see.o src/threads.o \
       src/time.o src/uci.o src/util.o src/WELL512a.o

MAIN = src/main.c

NAME = robocide

all: $(OBJS)
	$(CC) $(MAIN) $(OBJS) -o $(NAME) $(CFLAGS)
$(OBJS):

clean:
	@rm -f $(OBJS)
