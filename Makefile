CC = gcc
CCFLAGS = -Wall -O2 -Wno-traditional
LFLAGS = -lm -lwayland-client -lrt $(shell pkg-config --libs freetype2) $(shell pkg-config --cflags freetype2) -lxkbcommon 

INCLUDE = include 
SRC = src

SRC_FILES := $(wildcard src/*.c)
SRC_FILES := $(filter-out src/test.c, $(SRC_FILES))
BIN = bin
TARGET=client

.PHONY: test bin 
test: $(BIN)
	
$(BIN):
	mkdir -p $(BIN)
	$(CC) $(SRC_FILES) $(CCFLAGS) $(LFLAGS) -I$(INCLUDE) -o $(BIN)/$(TARGET)

run: $(BIN)
	./$(BIN)/$(TARGET)

clean:
	rm -rf $(BIN)
