CC = gcc
CFLAGS = -Wall -O3 -DNDEBUG

OBJS = src/fen.o src/pos.o

MAIN = src/main.c

NAME = robocide

all: $(OBJS)
	$(CC) $(MAIN) $(OBJS) -o $(NAME) $(CFLAGS)
$(OBJS):

clean:
	@rm -f $(OBJS)
