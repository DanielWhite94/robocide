TARGET = robocide
CC = gcc
CFLAGS = -pthread -Wall -O3 -flto -Wno-unused-local-typedefs -march=native
LFLAGS = -lm
CFLAGSNOBUILTIN = -DBUILTINS
CFLAGSDEBUG = -DNDEBUG

.PHONY: default all nobuiltin debug tune clean

default: $(TARGET)
all: default
nobuiltin: CFLAGSNOBUILTIN :=
debug: CFLAGSDEBUG :=
tune: CFLAGS += -DTUNE
nobuiltin: default
debug: default
tune: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) $(CFLAGSDEBUG) $(CFLAGSNOBUILTIN) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) $(CFLAGSDEBUG) $(CFLAGSNOBUILTIN) -o $@ $(LFLAGS)

clean:
	@rm -f *.o
