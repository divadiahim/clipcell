CC = gcc
CCFLAGS = -Wall -O2 -Wno-traditional
LFLAGS = -lm -lwayland-client -lrt $(shell pkg-config --libs freetype2) $(shell pkg-config --cflags freetype2) -lxkbcommon 

SRC_FILES_CLIP := $(wildcard src/clip/*.c)
SRC_FILES_SRV := $(wildcard src/server/*.c)
SRC_FILES_SRV := $(filter-out src/server/client.c, $(SRC_FILES_SRV))

BIN = bin
INCLUDE = include 
	
$(BIN)/client: $(SRC_FILES_CLIP)
	mkdir -p $(BIN)
	$(CC) $(CCFLAGS) -I$(INCLUDE) -o $@ $^ $(LFLAGS)

run: client
	./client

server: $(BIN)/server

$(BIN)/server: $(SRC_FILES_SRV)
	mkdir -p $(BIN)
	$(CC) $(CCFLAGS) -o $@ $^

clean:
	rm -rf $(BIN)
