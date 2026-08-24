TARGET = robocide
CC = gcc
CFLAGS = -pthread -Wall -flto -Wno-unused-local-typedefs -march=native -DNNUE_ARCH_MIN #-DEVALINFO
LFLAGS = -lm
CFLAGSNDEBUG = -O3 -DNDEBUG
CFLAGSNOBUILTIN = -DBUILTINS

.PHONY: default all nobuiltin debug tune clean

default: $(TARGET)
all: default
nobuiltin: CFLAGSNOBUILTIN :=
debug: CFLAGSNDEBUG :=
debug: CFLAGSDEBUG := -O2 -g
tune: CFLAGS += -DTUNE
nobuiltin: default
debug: default
tune: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c)) $(patsubst %.c, %.o, $(wildcard nnue/*.c)) syzygy/tbprobe.o syzygy/wrapper.o
HEADERS = $(wildcard *.h) $(wildcard syzygy/*.h) $(wildcard nnue/*.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) $(CFLAGSNDEBUG) $(CFLAGSDEBUG) $(CFLAGSNOBUILTIN) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) $(CFLAGSNDEBUG) $(CFLAGSDEBUG) $(CFLAGSNOBUILTIN) -o $@ $(LFLAGS)

clean:
	@rm -f *.o
	@rm -f nnue/*.o
	@rm -f syzygy/*.o
