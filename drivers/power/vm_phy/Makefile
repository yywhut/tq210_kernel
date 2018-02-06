#Makefile 2.6 
obj-m :=vmaddr_to_phy.o  
KERNEL :=/opt/EmbedSky/TQ210/Kernel_3.0.8_TQ210_for_Android_v1.0/  
PWD :=$(shell pwd)  
modules :  
	$(MAKE) -C $(KERNEL) M=$(PWD) modules  
.PHONEY:clean  
clean :  
	rm -f *.o *.ko  
