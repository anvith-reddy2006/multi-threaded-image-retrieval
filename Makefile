CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=gnu11
LDFLAGS = -lpng -lm -lpthread

SRC     = src
COMMON  = $(SRC)/common.c

.PHONY: all clean

all: retrieval_seq retrieval_mt retrieval_pool retrieval_pool_barrier

retrieval_seq: $(SRC)/sequential/retrieval.c $(COMMON) $(SRC)/common.h
	$(CC) $(CFLAGS) -o $@ $(SRC)/sequential/retrieval.c $(COMMON) $(LDFLAGS)

retrieval_mt: $(SRC)/multithreading/retrieval_mt.c $(COMMON) $(SRC)/common.h
	$(CC) $(CFLAGS) -o $@ $(SRC)/multithreading/retrieval_mt.c $(COMMON) $(LDFLAGS)

retrieval_pool: $(SRC)/threadpool/retrieval_threadpool.c $(COMMON) $(SRC)/common.h
	$(CC) $(CFLAGS) -o $@ $(SRC)/threadpool/retrieval_threadpool.c $(COMMON) $(LDFLAGS)

retrieval_pool_barrier: $(SRC)/threadpool/retrieval_pool_barrier.c $(COMMON) $(SRC)/common.h
	$(CC) $(CFLAGS) -o $@ $(SRC)/threadpool/retrieval_pool_barrier.c $(COMMON) $(LDFLAGS)

clean:
	rm -f retrieval_seq retrieval_mt retrieval_pool retrieval_pool_barrier
