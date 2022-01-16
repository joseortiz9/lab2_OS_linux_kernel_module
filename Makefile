obj-m += kernel_module.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	sudo insmod ./kernel_module.ko
	gcc ioctl.c
	sudo ./a.out
module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f a.out
	#rm -f char_dev
	sudo rmmod kernel_module