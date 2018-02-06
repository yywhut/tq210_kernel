#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/gfp.h>
#include <linux/string.h>
#include <linux/mm_types.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <asm/io.h>

#include <linux/miscdevice.h>




#include <linux/mm.h>		/* everything */
#include <linux/errno.h>	/* error codes */
#include <asm/pgtable.h>
#include <linux/fs.h>

#define	mmap_printk(args...) do {printk(KERN_ALERT "MMAP_DEMO " args); } while(0)
#define	KSTR_DEF	"hello world from kernel virtual space"
#define	mmap_name	"mmap_demo"

static struct page *pg;
static struct timer_list timer;

unsigned long kernel_memaddr = 0;
unsigned long pa_memaddr= 0;
unsigned long kernel_memsize= 0;

struct vm_operations_struct vm_ops;


/*alloc 2 page. 8192 bytes*/
#define PAGE_ORDER                  2   //this is very important.we can only allocat 2^PAGE_ORDER pages
										//PAGES_NUMBER must equal 2^PAGE_ORDER !!!!!!!!!!!!!!!!!!!!!!!!

/*this value can get from PAGE_ORDER*/
#define PAGES_NUMBER     			4


static void
timer_func (unsigned long data)
{
  printk ("timer_func:%s\n", (char *) data);
  timer.expires = jiffies + HZ * 30;
  add_timer (&timer);
}

static int
demo_open (struct inode *inode, struct file *filp)
{
  mmap_printk ("mmap_demo device open\n");
  return 0;
}

static int
demo_release (struct inode *inode, struct file *filp)
{
  mmap_printk ("mmap_demo device closed\n");
  return 0;
}

void vma_open(struct vm_area_struct *vma)
{
	//struct scullp_dev *dev = vma->vm_private_data;

	//dev->vmas++;
}

void vma_close(struct vm_area_struct *vma)
{
	//struct scullp_dev *dev = vma->vm_private_data;

	//dev->vmas--;
}







static int
demo_mmap (struct file *filp, struct vm_area_struct *vma)
{
  int err = 0;
  unsigned long start = vma->vm_start;
  unsigned long size = vma->vm_end - vma->vm_start;


	printk ("demo_mmap start\n");
	mmap_printk ("start is %x\n",start);
	mmap_printk ("size is %d\n",size);
  mmap_printk ("vma->vm_pgoff is %d\n",vma->vm_pgoff);

  vma->vm_ops = &vm_ops;
	vma->vm_flags |= VM_RESERVED;
	vma->vm_private_data = filp->private_data;
	vma_open(vma);

	printk ("demo_mmap stop\n");

  //err = remap_pfn_range (vma, start, vma->vm_pgoff, size, vma->vm_page_prot);
 // err = remap_pfn_range (vma, start, (pa_memaddr >> PAGE_SHIFT), size, vma->vm_page_prot);
  return err;
}
static struct file_operations mmap_fops = {
  .owner = THIS_MODULE,
  .open = demo_open,
  .release = demo_release,
  .mmap = demo_mmap,

};





static int vma_nopage(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	unsigned long offset;
	//struct scullv_dev *ptr, *dev = vma->vm_private_data;
	struct page *page = NULL;
	char *pageptr = NULL; /* default to "missing" */
	int retval = VM_FAULT_NOPAGE;

	//down(&dev->sem);
	offset = (unsigned long)(vmf->virtual_address - vma->vm_start) + (vma->vm_pgoff << PAGE_SHIFT);
	//  vmf->virtual_address 是这一次出错的地址，每个page不一样，往后移动
	// vma->vm_start 是内核已经分配好的这16k的总的虚拟地址，
	//vma->vm_pgoff  是user在做mmap的时候  对着16k偏移多少来做映射，比如4096 就是从第二个page开始映射
	//if (offset >= dev->size) goto out; /* out of range */

	printk ("vma_nopage start\n");
	
	printk ("offset is %d \n",offset);

	// 上层在访问某一个地址的时候出现错误，出错的地址的信息在  vmf->virtual_address
	// unsigned long start = vma->vm_start; 而这个是内核预先分配好的16k这一段内存的总的起始地址
	printk ("vmf->virtual_address = %x\n", vmf->virtual_address);
	printk ("vma->vm_start = %x\n", vma->vm_start);  
	printk ("vma->vm_pgoff = %d\n", vma->vm_pgoff);

	/*
	 * Now retrieve the scullv device from the list,then the page.
	 * If the device has holes, the process receives a SIGBUS when
	 * accessing the hole.
	 */
	//offset >>= PAGE_SHIFT; /* offset is a number of pages */


	
	pageptr = kernel_memaddr + offset;//指向出错的实际虚拟地址的位置
	printk ("kernel_memaddr = %p\n", kernel_memaddr);
	printk ("pageptr = %p\n", pageptr);
	if (!pageptr) goto out; /* hole or end-of-file */

	/*
	 * After scullv lookup, "page" is now the address of the page
	 * needed by the current process. Since it's a vmalloc address,
	 * turn it into a struct page.
	 */
	//page = vmalloc_to_page(pageptr);

	printk ("sssssssss\n");
	page = virt_to_page(pageptr); //  内核虚拟地址，得到实际的物理page
	printk ("111111\n");

	/* got it, now increment the count */
	get_page(page);
	printk ("22222\n");
	vmf->page = page;  // 更新这个page即可
	retval = 0;
	printk ("vma_nopage stop\n");

	

  out:
	//up(&dev->sem);
	return retval;
}


struct vm_operations_struct vm_ops = {
	.open =     vma_open,
	.close =    vma_close,
	.fault =   vma_nopage,
};



static struct miscdevice misc = {
  .minor = MISC_DYNAMIC_MINOR,
  .name = mmap_name,
  .fops = &mmap_fops,
};

static int __init
demo_map_init (void)
{
  int ret = 0;

  char *kstr;
	struct page *page;
	int i;

/*

  pg = alloc_pages (GFP_HIGHUSER, 0);

  SetPageReserved (pg);

  kstr = (char *) kmap (pg);
  strcpy (kstr, KSTR_DEF);
  printk ("kpa = 0x%lx, kernel string = %s\n", page_to_phys (pg), kstr);


	
  init_timer (&timer);
  timer.function = timer_func;
  timer.data = (unsigned long) kstr;
  timer.expires = jiffies + HZ * 10;
  add_timer (&timer);

  */


 	kernel_memaddr =__get_free_pages(GFP_KERNEL, PAGE_ORDER);
	printk("Allocate memory kernel_memaddr is %x", kernel_memaddr);

    if(!kernel_memaddr)
    {
		printk("Allocate memory failure!\n");
	}
	else
	{
		page = virt_to_page(kernel_memaddr );
		for(i = 0;i < (1 << PAGE_ORDER);i++)
		{
			SetPageReserved(page);
			page++;
		}
		kernel_memsize = PAGES_NUMBER * PAGE_SIZE;
		printk("Allocate memory success!. The phy mem addr=%08lx, size=%lu\n", __pa(kernel_memaddr), kernel_memsize);
	}
	init_timer (&timer);
  timer.function = timer_func;
  timer.data = (char *) kernel_memaddr;
  timer.expires = jiffies + HZ * 10;
  add_timer (&timer);

	
	pa_memaddr = __pa(kernel_memaddr);

		printk ("pa_memaddr is %d\n", pa_memaddr);
		printk ("kernel_memaddr is %x\n", kernel_memaddr);
		printk ("kernel_memaddr is %d\n", kernel_memaddr);
		printk ("kernel_memaddr is %p\n", kernel_memaddr);

	strcpy (kernel_memaddr, KSTR_DEF);
  	printk ("kpa = %p, kernel string = %s\n", kernel_memaddr, kernel_memaddr);
	
	strcpy (kernel_memaddr + 4096, "woshiyy");

  	printk ("kpa = %p, kernel string = %s\n", kernel_memaddr+ 4096, kernel_memaddr + 4096);

  ret = misc_register (&misc);
  printk ("the mmap miscdevice registered\n");
  return ret;
}

module_init (demo_map_init);

static void
demo_map_exit (void)
{
	struct page *page;
	int i;

  del_timer_sync (&timer);

  misc_deregister (&misc);
  printk ("the device misc_mmap deregistered\n");


	page = virt_to_page(kernel_memaddr);
	for (i = 0; i < (1 << PAGE_ORDER); i++)
	{
		ClearPageReserved(page);
		page++;
	}
	//ClearPageReserved(virt_to_page(kernel_memaddr));
	free_pages(kernel_memaddr, PAGE_ORDER);

  //kunmap (pg);
  //ClearPageReserved (pg);
 // __free_pages (pg, 0);
}

module_exit (demo_map_exit);
MODULE_LICENSE ("DUAL BSD/GPL");
MODULE_AUTHOR ("BG2BKK");
