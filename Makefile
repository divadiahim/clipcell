CC = gcc
CCFLAGS = -Wall -O2 -Wno-traditional
LFLAGS = -lm -lwayland-client -lrt $(shell pkg-config --libs freetype2) $(shell pkg-config --cflags freetype2) 

INCLUDE = include 
SRC = src
BIN = bin
TARGET=client

.PHONY: test bin
test: $(BIN)
	
$(BIN):
	mkdir -p $(BIN)
	$(CC) $(SRC)/*.c $(CCFLAGS) $(LFLAGS) -I$(INCLUDE) -o $(BIN)/$(TARGET)

run: $(BIN)
	./$(BIN)/$(TARGET)

clean:
	rm -rf $(BIN)