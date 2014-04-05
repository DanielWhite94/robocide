CC = gcc
CFLAGS = -Wall -O3 -DNDEBUG

OBJS = src/attacks.o src/bb.o src/fen.o src/magicmoves.o src/pos.o src/perft.o \
       src/time.o

MAIN = src/main.c

NAME = robocide

all: $(OBJS)
	$(CC) $(MAIN) $(OBJS) -o $(NAME) $(CFLAGS)
$(OBJS):

clean:
	@rm -f $(OBJS)
