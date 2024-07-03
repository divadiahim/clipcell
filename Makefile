all: clipcell clipcelld

.PHONY: default install clean

CC = gcc
CCFLAGS = -Wall -O2 -Wno-traditional
LFLAGS = -lm -lwayland-client -lrt $(shell pkg-config --libs freetype2) $(shell pkg-config --cflags freetype2) $(shell pkg-config --libs libmagic) -lxkbcommon -lpng -lc
SRV_LFLAGS = $(shell pkg-config --libs libmagic) 

SRC_FILES_CLIP := $(wildcard src/*.c)
SRC_FILES_CLIP := $(filter-out src/clipcelld.c, $(SRC_FILES_CLIP))
SRC_FILES_SRV := $(wildcard src/clipcelld.c src/map.c)

BIN = bin
INCLUDE = include 
	
$(BIN)/clipcell: $(SRC_FILES_CLIP)
	mkdir -p $(BIN)
	$(CC) $(CCFLAGS) -I$(INCLUDE) -o $@ $^ $(LFLAGS)

$(BIN)/clipcelld: $(SRC_FILES_SRV)
	mkdir -p $(BIN)
	$(CC) $(CCFLAGS) -o $@ $^ $(SRV_LFLAGS)

clipcell: $(BIN)/clipcell

clipcelld: $(BIN)/clipcelld

install: $(BIN)/clipcell $(BIN)/clipcelld
	cp $(BIN)/clipcell /usr/bin/clipcell	
	cp $(BIN)/clipcelld /usr/bin/clipcelld

clean:
	rm -rf $(BIN)
