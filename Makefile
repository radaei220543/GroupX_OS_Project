CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -pthread
SRC_DIR = src

.PHONY: all server client operations clean test

all: server client operations

server: $(SRC_DIR)/server.c
	$(CC) $(CFLAGS) -o server $(SRC_DIR)/server.c

client: $(SRC_DIR)/client.c
	$(CC) $(CFLAGS) -o client $(SRC_DIR)/client.c

operations: $(SRC_DIR)/operations.c
	$(CC) $(CFLAGS) -o operations $(SRC_DIR)/operations.c

test: all
	./run_test.sh

clean:
	rm -f server client operations \
	      original.dat reassembled.dat \
	      result_min.txt result_max.txt result_sorted.dat \
	      execution_log.txt server.log client.log build.log
