EXTRA_CFLAGS +=
APP_EXTRA_FLAGS:= -O2 -ansi -pedantic
KERNEL_SRC:= /lib/modules/$(shell uname -r)/build
SUBDIR= $(PWD)
GCC:=gcc
RM:=rm

.PHONY : clean

all: clean modules work monitor

obj-m:= mp3.o

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(SUBDIR) modules

work: work.c
	$(GCC) -o work work.c

monitor: monitor.c
	$(GCC) -o monitor monitor.c

t1:
	nice ./work 1024 R 50000 & nice ./work 1024 R 10000 &

t2:
	nice ./work 1024 R 50000 & nice ./work 1024 L 10000 &
clean:
	$(RM) -f *~ *.ko* *.o* *.mod.c Module.symvers modules.order
