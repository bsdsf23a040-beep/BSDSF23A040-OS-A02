CC = gcc
CFLAGS = -Wall -g
TARGET = bin/ls
SRC = src/ls-v1.0.0.c

all:
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)
