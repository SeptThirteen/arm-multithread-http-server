CC ?= gcc
AR ?= ar
CFLAGS ?= -O2 -Wall -Wextra -std=c11 -pthread -Iinclude
LDFLAGS ?= -pthread
LDLIBS ?= -lsqlite3

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
TARGET := server

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

run: $(TARGET)
	./$(TARGET) 8080 8

.PHONY: all clean run
