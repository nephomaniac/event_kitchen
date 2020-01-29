# CC=gcc
IDIR = "./includes"
PKG_SOURCE_VERSION:=$(COMMIT)
#INCLUDES = "-I/usr/include/libnl3"

## ubuntu pre-reqs...
# apt-get install libmosquitto-dev
# apt-get install libjansson-dev
# sudo apt-get install libuv1-dev
CFLAGS= -g -O2 -Wall -Wextra -Isrc -I$(IDIR) $(INCLUDES)

ODIR=obj
LDIR =../lib

#LIBS=-lpthread -ljansson -lmosquitto  -luci
LIBS+=-lpthread -ljansson


_DEPS=$(wildcard *.h include/**/*.h include/*.h)
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))
SOURCES=$(wildcard *.c src/**/*.c src/*.c)
OBJECTS=$(patsubst %.c,$(ODIR)/%.o,$(SOURCES))
$(info OBJECTS is [${OBJECTS}])
$(info \@ is [${@}])
$(info \$^ is [${^}])
$(info SOURCES is [${SOURCES}])
$(info DEPS is [${DEPS}])
$(info _DEPS is [${_DEPS}])

$(ODIR)/%.o: %.c $(DEPS)
	@mkdir -p obj
	$(CC) -g -c -o $@ $< $(CFLAGS)

define uv_tests
	TARGET=$(1)
	MAINSRC:=tests/uv/$(1).c
	LIBS+= -luv
endef

define fs_tests
	TARGET=$(1)
	MAINSRC:=tests/inotify/$(1).c
endef

define mosquitto_tests
	TARGET=$(1)
	MAINSRC:=tests/mosquitto/$(1).c
	LIBS+= -lmosquitto
endef

# UV event Tests....
uv_locks: $(OBJECTS)
	$(clean)
	$(eval $(call uv_tests,$(@)))
	#$(info Building TARGET:[$(TARGET)], with LIBS is [${LIBS}], CFLAGS:[$[CFPLAGS}])
	$(CC) $(MAINSRC) -o $(TARGET) $^ $(CFLAGS) $(LIBS)

libuv_example: 
	$(eval $(call uv_tests,$(@)))
	$(CC) $(MAINSRC) -o $(TARGET) $^ $(CFLAGS) $(LIBS)

uv_dir: 
	$(eval $(call uv_tests,$(@)))
	$(CC) $(MAINSRC) -o $(TARGET) $^ $(CFLAGS) $(LIBS)

uv_int: 
	$(eval $(call uv_tests,$(@)))
	$(CC) $(MAINSRC) -o $(TARGET) $^ $(CFLAGS) $(LIBS)

uv_mqtt: 
	$(eval $(call uv_tests,$(@)))
	$(CC) $(MAINSRC) -o $(TARGET) $^ $(CFLAGS) $(LIBS)

uv_timeout: 
	$(eval $(call uv_tests,$(@)))
	$(CC) $(MAINSRC) -o $(TARGET) $^ $(CFLAGS) $(LIBS)

# Inotify File System Event Tests....
inot_dir_test: 
	$(eval $(call fs_tests,$(@)))
	$(CC) $(MAINSRC) -o $(TARGET) $^ $(CFLAGS) $(LIBS)

# Mosquitto Tests....
mosq_handler: $(OBJECTS)
	$(eval $(call mosquitto_tests,$(@)))
	$(CC) $(MAINSRC) -o $(TARGET) $^ $(CFLAGS) $(LIBS)

# UBUS Tests

.PHONY: clean

clean:
	rm -f *.o
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~

all: ; @echo $($(TARGET)_SRC)
