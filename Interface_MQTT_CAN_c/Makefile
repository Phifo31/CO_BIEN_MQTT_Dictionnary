

CC=gcc
CFLAGS=-W -Wall -ansi -Wextra -Wpedantic -std=c11 -Iinclude -D _POSIX_C_SOURCE=200809L
LDFLAGS=
EXEC=cobien_bridge

SRC=src/bridge_app.c \
  src/pack.c \
  src/table.c \
  src/mqtt_io.c \
  src/can_io.c 

OBJ=build/bridge_app.o \
  build/pack.o \
  build/table.o \
  build/mqtt_io.o \
  build/can_io.o

INCLUDE = include/pack.h \
  include/table.h \
  include/mqtt_io.h \
  include/can_io.h \
  include/log.h 
  
all: $(EXEC)

doc : $(SRC) Makefile
	doxygen 

cobien_bridge : $(OBJ) Makefile
	$(CC) -o $@ $(OBJ) -lmosquitto -lcjson $(LDFLAGS)

build/pack.o : src/pack.c $(INCLUDE) Makefile
	mkdir -p build
	$(CC) -o $@ -c $< $(CFLAGS)

build/bridge_app.o : src/bridge_app.c $(INCLUDE)  Makefile
	mkdir -p build
	$(CC) -o $@ -c $< $(CFLAGS)

build/table.o : src/table.c $(INCLUDE) Makefile
	mkdir -p build
	$(CC) -o $@ -c $< $(CFLAGS)

build/mqtt_io.o : src/mqtt_io.c $(INCLUDE) Makefile
	mkdir -p build
	$(CC) -o $@ -c $< $(CFLAGS)

build/can_io.o : src/can_io.c $(INCLUDE)  Makefile
	mkdir -p build
	$(CC) -o $@ -c $< $(CFLAGS)

clean:
	rm -Rf build

distclean:
	rm -Rf build
	rm $(EXEC)
	rm -Rf html





#  pack.o \
#  table.o \
#  mqtt_io.o \
#  can_io.o 

#%.o: %.c
#	$(CC) -o build/$@ -c $< $(CFLAGS)


