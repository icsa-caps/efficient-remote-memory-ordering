CC      := gcc
CFLAGS  := -O2 -Wall
LDLIBS  := -lrdmacm -libverbs -lpthread

TARGETS := rdma_server read_bench write_bench write_lat write_bf_lat write_bf_inline_lat 

all: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(TARGETS)
