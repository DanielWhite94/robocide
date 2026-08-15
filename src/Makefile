TARGET = robocide
CC = gcc
CFLAGS = -pthread -Wall -O3 -flto -Wno-unused-local-typedefs -march=native -DNNUE_ARCH_PS
LFLAGS = -lm
CFLAGSDEBUG = -DNDEBUG -g #-DEVALINFO
CFLAGSNOBUILTIN = -DBUILTINS

.PHONY: default all nobuiltin debug tune clean

default: $(TARGET)
all: default
nobuiltin: CFLAGSNOBUILTIN :=
debug: CFLAGSDEBUG :=
tune: CFLAGS += -DTUNE
nobuiltin: default
debug: default
tune: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c)) $(patsubst %.c, %.o, $(wildcard nnue/*.c)) syzygy/tbprobe.o syzygy/wrapper.o
HEADERS = $(wildcard *.h) $(wildcard syzygy/*.h) $(wildcard nnue/*.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) $(CFLAGSDEBUG) $(CFLAGSNOBUILTIN) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) $(CFLAGSDEBUG) $(CFLAGSNOBUILTIN) -o $@ $(LFLAGS)

clean:
	@rm -f *.o
	@rm -f nnue/*.o
	@rm -f syzygy/*.o
