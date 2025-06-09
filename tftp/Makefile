CC=gcc
CFLAGS=-Wall -Werror -g -pthread 
BIN=./bin

PROGS=server-tftp-concurrente

.PHONY: all
all: $(PROGS)

LIST=$(addprefix $(BIN)/, $(PROGS))

server-tftp-concurrente: server-tftp-concurrente.c 
	$(CC) -o bin/$@ $^ $(CFLAGS)

.PHONY: clean
clean:
	rm -f $(LIST)

zip:
	git archive --format zip --output ${USER}-TP4.zip HEAD
