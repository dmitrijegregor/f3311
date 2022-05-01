obj-m += hello.o

all:
 make -C kernel-3.18/out/ M=$(PWD) modules
clean:
 make -C kernel-3.18/out/ M=$(PWD) clean
