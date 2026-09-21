# Boltzmann in C.
#
# Non-recursive, one binary (ludwig) plus unit tests under tests/.
#   make            build ludwig
#   make check      build and run the unit tests
#   make compare    differential test against the Python implementation
#   make clean

CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -I.
LDLIBS += -lcurl -lm

COMMON_SRCS := \
	common/tal.c \
	common/utils.c \
	common/u64map.c \
	common/pyfmt.c \
	common/json.c \
	common/http.c

BOLTZMANN_SRCS := \
	boltzmann/transaction.c \
	boltzmann/txos_linker.c \
	boltzmann/perfect_cj.c \
	boltzmann/tx_processor.c \
	boltzmann/display.c

PROVIDER_SRCS := \
	providers/blockchain_info.c \
	providers/esplora.c \
	providers/bitcoind_rpc.c \
	providers/file.c

LIB_SRCS := $(COMMON_SRCS) $(BOLTZMANN_SRCS) $(PROVIDER_SRCS)
LIB_OBJS := $(LIB_SRCS:.c=.o)

TEST_SRCS := $(wildcard tests/run-*.c)
TEST_BINS := $(TEST_SRCS:.c=)

all: ludwig

ludwig: ludwig.o $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

tests/run-%: tests/run-%.c $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

check: $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do echo "== $$t"; ./$$t; done

compare: ludwig
	tests/compare_with_python.sh

clean:
	rm -f ludwig ludwig.o $(LIB_OBJS) $(TEST_BINS)
	rm -rf tests/*.dSYM

.PHONY: all check compare clean
