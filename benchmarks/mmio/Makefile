obj-m += mmio_bench.o

PWD := $(CURDIR)

all:
	make -C /lib/modules/$(shell uname -r)/build CC=$(CC) M=$(PWD) modules EXTRA_CFLAGS="-mavx2"

clean:
	make -C /lib/modules/$(shell uname -r)/build CC=$(CC) M=$(PWD) clean 
