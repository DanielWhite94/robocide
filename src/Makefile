TARGET = robocide
CC = gcc
CFLAGS = -pthread -lm -Wall -O3 -flto -Wno-unused-local-typedefs
CFLAGSDEBUG = -DNDEBUG

.PHONY: default all debug stats tune clean

default: $(TARGET)
all: default
debug: CFLAGSDEBUG := 
tune: CFLAGS += -DTUNE
stats: CFLAGS += -DSTATS
debug: default
stats: default
tune: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) $(CFLAGSDEBUG) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) $(CFLAGSDEBUG) -o $@

clean:
	@rm -f *.o
	@rm -f $(TARGET)
