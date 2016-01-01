TARGET = robocide
CC = gcc
CFLAGS = -pthread -Wall -O3 -flto -Wno-unused-local-typedefs
LFLAGS = -lm
CFLAGSDEBUG = -DNDEBUG

.PHONY: default all debug tune clean

default: $(TARGET)
all: default
debug: CFLAGSDEBUG :=
tune: CFLAGS += -DTUNE
debug: default
tune: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) $(CFLAGSDEBUG) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) $(CFLAGSDEBUG) -o $@ $(LFLAGS)

clean:
	@rm -f *.o
	@rm -f $(TARGET)
