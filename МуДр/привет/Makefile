obj-m += hello.o

all:
 make -C kernel-3.18/out/ M=$(shell pwd) modules
clean:
 make -C kernel-3.18/out/ M=$(shell pwd) clean
