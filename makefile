# Compiler and flags
CC = gcc
CFLAGS = -std=c17 -Wall -Werror -Wextra
SDL_FLAGS = $(shell sdl2-config --cflags --libs)

# Target
TARGET = chip8

# Source
SRC = chip8.c

# Default build
all: $(TARGET)

# Regular build
$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(SDL_FLAGS)

# Debug build
debug: $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(SDL_FLAGS) -DDEBUG

clean:
	rm -f $(TARGET)

.PHONY: all debug clean