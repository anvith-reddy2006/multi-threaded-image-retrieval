CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=gnu11
LDFLAGS = -lpng -lm -lpthread

SRC     = src
IMG     = $(SRC)/common/image_processing.c
IMGH    = $(SRC)/common/image_processing.h

.PHONY: all clean

all: retrieval_seq retrieval_mt retrieval_pool

retrieval_seq: $(SRC)/sequential/retrieval.c $(IMG) $(IMGH)
	$(CC) $(CFLAGS) -o $@ $(SRC)/sequential/retrieval.c $(IMG) $(LDFLAGS)

retrieval_mt: $(SRC)/multithreading/retrieval_mt.c $(IMG) $(IMGH)
	$(CC) $(CFLAGS) -o $@ $(SRC)/multithreading/retrieval_mt.c $(IMG) $(LDFLAGS)

retrieval_pool: $(SRC)/threadpool/retrieval_threadpool.c $(IMG) $(IMGH)
	$(CC) $(CFLAGS) -o $@ $(SRC)/threadpool/retrieval_threadpool.c $(IMG) $(LDFLAGS)

clean:
	rm -f retrieval_seq retrieval_mt retrieval_pool
