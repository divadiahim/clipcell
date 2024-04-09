CC = gcc
CCFLAGS = -Wall -O2 -Wno-traditional
LFLAGS = -lm -lwayland-client -lrt $(shell pkg-config --libs freetype2) $(shell pkg-config --cflags freetype2) -lxkbcommon 

SRC_FILES := $(wildcard src/*.c)
SRC_FILES := $(filter-out src/test.c, $(SRC_FILES))
BIN = bin
INCLUDE = include 
	
client: $(SRC_FILES)
	mkdir -p $(BIN)
	$(CC) $(CCFLAGS) -I$(INCLUDE) -o $@ $^ $(LFLAGS)

run: client
	./$(BIN)/$(TARGET)

clean:
	rm -rf $(BIN)
