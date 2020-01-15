# CC=gcc
IDIR = "./includes"
PKG_SOURCE_VERSION:=$(COMMIT)
#INCLUDES = "-I/usr/include/libnl3"

## ubuntu pre-reqs...
# apt-get install libmosquitto-dev
# apt-get install libjansson-dev

CFLAGS= -g -O2 -Wall -Wextra -Isrc -I$(IDIR) $(INCLUDES)

ODIR=obj
LDIR =../lib

#LIBS=-lpthread -ljansson -lmosquitto  -luci
LIBS+=-lpthread -ljansson -lmosquitto

TARGET=test
SO_TARGET=$(patsubst %.a,%.so,$(TARGET))

_DEPS=$(wildcard *.h include/**/*.h include/*.h)
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

SOURCES=$(wildcard *.c src/**/*.c src/*.c)
OBJECTS=$(patsubst %.c,$(ODIR)/%.o,$(SOURCES))

$(ODIR)/%.o: %.c $(DEPS)
	@mkdir -p obj
	$(CC) -g -c -o $@ $< $(CFLAGS)

$(SO_TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f *.o
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~
