obj-m += mmio_bench.o

PWD := $(CURDIR)
USE_SFENCE ?= 0

all:
	make -C /lib/modules/$(shell uname -r)/build CC=$(CC) M=$(PWD) modules EXTRA_CFLAGS="-mavx2 -DUSE_SFENCE=$(USE_SFENCE)"

clean:
	make -C /lib/modules/$(shell uname -r)/build CC=$(CC) M=$(PWD) clean
