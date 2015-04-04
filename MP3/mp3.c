#include <linux/module.h>   /* Needed by all modules */
#include <linux/kernel.h>   /* Needed for KERN_INFO */
#include <linux/init.h>     /* Needed for the macros */

#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include "mp3_given.h"

#define TIMER_DELAY 5000
#define NUM_PAGES 128
#define WORK_DELAY 50
#define DEVICE_NAME "MP3_CDEV" 
static DEFINE_MUTEX(lock);
static LIST_HEAD(plist_head);
struct workqueue_struct *workq;
struct delayed_work mp3_delayed_work;
static unsigned long *samples_buffer;
unsigned long samples_buffer_write_index;
static unsigned long sample_num;
static unsigned long max_samples;
static dev_t mp3_dev_num;
static struct cdev* mp3_dev_obj;

typedef struct _process_meta_node {
    int pid;
    unsigned long cpu_util;
    unsigned long major_fault;
    unsigned long minor_fault;
    struct task_struct* task_pcb;
    struct list_head list;
} process_meta_node;

static struct proc_dir_entry *mp3_root;
static struct proc_dir_entry *mp3_status;
ssize_t status_read(struct file *filp, char __user *buff,
    size_t count, loff_t *offp);
ssize_t status_write(struct file *filp, const char __user *buff,
    size_t count, loff_t *offp);
int _is_pid_registered(long int target_pid);
int get_process_info_list_as_string(char *op);
int register_process(int pid);
int delete_process_list(void);
static void work_callback(void* data);
static void delete_workq(void);
static int schedule_workqueue_job(void);
static void collect_profile_sample(void);
static int alloc_cdevice_num(void);
static int free_cdevice_num(void);
static int add_cdevice_obj(void);
static void remove_cdevice_obj(void);

static int cdevice_open(struct inode* p, struct file* f)
{
    printk(KERN_INFO "%s open called\n", DEVICE_NAME);
    return 0;
}

static int cdevice_release(struct inode* node, struct file* f)
{
    printk(KERN_NOTICE "%s release called\n", DEVICE_NAME);
    return 0;
}

static int cdevice_map(struct file* f, struct vm_area_struct* varea) {
    int i = 0;
    int retval = 0;
    int num_elements_per_page = PAGE_SIZE / sizeof(unsigned long);
    for (i = 0; i < NUM_PAGES; i++) {
        void* virtual_address_base = (void*)(samples_buffer + i * num_elements_per_page);
        unsigned long phy_address_base = vmalloc_to_pfn(virtual_address_base);
        if(remap_pfn_range(varea, varea->vm_start + i * PAGE_SIZE, phy_address_base, PAGE_SIZE, varea->vm_page_prot) < 0) {
            printk(KERN_ALERT "mmap failed\n");
            retval = -EAGAIN;
            goto exit;
        }
    }
    printk(KERN_NOTICE "%s mmap done\n", DEVICE_NAME);
exit:
    return retval;
}

struct file_operations mp3_cdev_fops = {
    .owner = THIS_MODULE,
    .open = cdevice_open,
    .release = cdevice_release,
    .mmap = cdevice_map,
};

static int alloc_cdevice_num(void) {
    int retval = 0;
    //  Dynamic Allocation of Major Numbers
    if (alloc_chrdev_region(&mp3_dev_num, 0, 1, DEVICE_NAME) != 0) {
        retval = -ENOMEM;
    }
    printk(KERN_INFO "alloc_cdevice_num retval=%d", retval);
    return retval;
}

static int free_cdevice_num(void) {
    unregister_chrdev_region(mp3_dev_num, 1);
    return 0;
}

static void remove_cdevice_obj(void) {
    if (mp3_dev_obj) {
        cdev_del(mp3_dev_obj);
        mp3_dev_obj = NULL;
    }
}
static int add_cdevice_obj(void) {
    int retval = 0;
    mp3_dev_obj = cdev_alloc();
    if (mp3_dev_obj == NULL) {
        retval = -ENOMEM;
        free_cdevice_num();
        goto exit;
    }

    mp3_dev_obj->ops = &mp3_cdev_fops;
    if (cdev_add(mp3_dev_obj, mp3_dev_num, 1) < 0) { 
        free_cdevice_num();
        retval = -ENOMEM;
        goto exit;
    }
    printk(KERN_INFO "add_cdevice_obj retval=%d", retval);
exit:
    return retval;
}
static void collect_profile_sample(void) {
    process_meta_node *curr;
    //0-jiffies, 1-minorfault, 2-majorfault, 3-cpututil
    unsigned long stats[4];
    int i = 0;
    unsigned long utime = 0, stime = 0;
    // zero the stats array
    for (i = 0; i < 4;i++) {
        stats[i] = 0;
    }
    
    mutex_lock(&lock);
    list_for_each_entry(curr, &plist_head, list) {
        if (get_cpu_use(curr->pid, &curr->minor_fault, &curr->major_fault,
                &utime, &stime) == 0) {
            // add only valid pid stats
            stats[1] += curr->minor_fault;
            stats[2] += curr->major_fault;
            curr->cpu_util = utime + stime;
            stats[3] += curr->cpu_util;
            printk(KERN_INFO "MP3:(%lu, %lu, %lu)\n", curr->minor_fault, curr->major_fault, stats[3]);
        } else {
            printk(KERN_ALERT "invalid pid:%d", curr->pid);
        }
    }
    mutex_unlock(&lock);

    // write to global buffer
    // defensive check. should never exceed!!
    printk(KERN_INFO "write_index=%lu\n", samples_buffer_write_index);
    if (samples_buffer_write_index >= 0 && samples_buffer_write_index + 4 <= max_samples) {
        printk(KERN_INFO "sample num=%lu\n", sample_num);
        samples_buffer[samples_buffer_write_index] = jiffies;
        for (i = 1; i < 4; i++) {
            samples_buffer[samples_buffer_write_index + i] = stats[i];
        }
    } else {
        printk(KERN_ALERT "something went wrong in write index:%lu\n", samples_buffer_write_index);
    }
    // wrap around the write counter if needed
    samples_buffer_write_index  = (samples_buffer_write_index + 4) % max_samples;
}

int delete_process_list() {
    process_meta_node *curr, *temp;
    printk(KERN_INFO "deleting pid list");
    mutex_lock(&lock);
    list_for_each_entry_safe(curr, temp, &plist_head, list) {
        list_del(&curr->list);
        kfree(curr); 
    }
    mutex_unlock(&lock);
    return 0;
}

int get_process_info_list_as_string(char *buff) {
    int size = 0;
    process_meta_node *curr;
    mutex_lock(&lock);
    list_for_each_entry(curr, &plist_head, list) {
        size = sprintf(buff, "%s %d\n", buff, curr->pid);
    }
    mutex_unlock(&lock);
    return size; 
}

int _is_pid_registered(long int target_pid) {
    int retval = 0;
    process_meta_node *curr;
    list_for_each_entry(curr, &plist_head, list) {
        if (curr->pid == target_pid) {
            retval = 1;
            break;
        }
    }
    return retval;
}

int unregister_process(int target_pid) {
    int retval = -1;
    process_meta_node *curr, *temp;
    mutex_lock(&lock);
    list_for_each_entry_safe(curr, temp, &plist_head, list) {
        if (curr->pid == target_pid) {
            list_del(&curr->list);
            kfree(curr);
            if (list_empty(&plist_head) > 0) {
                // last process deleted
                // destroy workqueue
                delete_workq();
            }
            retval = 0;
            goto exit;
        }
    }
exit:
    mutex_unlock(&lock);
    return retval;
}

int register_process(int pid) {
    int retval = 0;
    int empty = 0;
    process_meta_node *pnode;
    pnode = (process_meta_node*)kmalloc(sizeof(process_meta_node), GFP_KERNEL);
    if (pnode == NULL) return -ENOMEM;
    pnode->pid = pid;
    pnode->task_pcb = find_task_by_pid(pid);
    pnode->cpu_util = 0;
    pnode->major_fault = 0;
    pnode->minor_fault = 0;
    
    mutex_lock(&lock);
    empty = list_empty(&plist_head) ;
    if (!empty) {
        // only if list not empty check for duplicate pid
        if(_is_pid_registered(pid)) {
            printk(KERN_INFO "pid %d already registerd", pid);
            retval = -EFAULT;
            goto exit;
        }
    }
    // added to head to save some time. order not important
    list_add(&pnode->list, &plist_head);
    // schedule the work queue if this is the first process
    if (empty) {
        // before starting delayed work, reset write pointer
        // and flush stale data in samples buffer
        // set the buffer to zero
        memset (samples_buffer, 0, NUM_PAGES * PAGE_SIZE);
        // write pointer in the cyclic quue
        samples_buffer_write_index = 0;
        printk(KERN_INFO "first process! schedule work\n");
        schedule_workqueue_job();
    }
    printk(KERN_INFO "pid %d added", pid);
    retval = 0; 
exit:
    mutex_unlock(&lock);
    return retval;
}

ssize_t status_read(struct file *filp, char __user *buff,
    size_t count, loff_t *offp)  {
    int retval = 0;
    printk(KERN_INFO "status read called");

    if (*offp > 0) return 0;
    char *temp=(char*)kmalloc(PAGE_SIZE*sizeof(char), GFP_KERNEL);
    if (temp == NULL) {
        retval = -ENOMEM;
        goto exit;
    }
    int size = get_process_info_list_as_string(temp);
    if (size > count) {
        //cap it
        printk(KERN_INFO "buffer insufficient!capping");
        size = count;
    }

    if (copy_to_user(buff, temp, size)) {
        retval = -EFAULT;
        goto exit;
    }
    retval = size;
    *offp += retval;
exit:
    if(temp) {
        kfree(temp);
    }
    return retval;
}

ssize_t status_write(struct file *filp, const char __user *buff,
    size_t count, loff_t *offp) {
    int retval = 0;
    if (count > PAGE_SIZE) {
        // cap it
        count = PAGE_SIZE;
    }
    char* input=(char*)kmalloc((count+1) * sizeof(char), GFP_KERNEL);
    if (input == NULL) {
        retval = -ENOMEM;
        goto exit;
    }

    if(copy_from_user(input, buff, count)) {
        retval = -EFAULT;
        goto exit;
    }
    input[count] = '\0';
    char type = input[0];
    char *temp = input + 2;
    long int pid = 0; 
    if (kstrtol(temp, 10, &pid) != 0) {
        retval = -EINVAL;
        goto exit;
    }

    //handle_msg(type, pid);
    switch(type) {
        case 'R':
            retval = register_process(pid);
            if (retval != 0) {
                goto exit;
            }
            printk(KERN_INFO "pid  %ld registered", pid);
            break;
        case 'U':
            retval = unregister_process(pid);
            if (retval != 0) {
                goto exit;
            }
            printk(KERN_INFO "pid  %ld unregistered", pid);
            break;
        default:
            retval = -EINVAL;
            goto exit;
    }
    //pid read successful
    retval = count;
exit:
    if (input) {
        kfree(input);
    }
    return retval;
}

/*
void _periodic_timer_callback(unsigned long);
void update_process_cpu_time(void);


void update_process_cpu_time(void) {
    process_meta_node *curr, *next;
    mutex_lock(&lock);
    unsigned long cputime = 0;
    list_for_each_entry_safe(curr, next, &plist_head, list) {
        if (get_cpu_use(curr->pid, &cputime) == 0) {
            printk(KERN_INFO "cpu time for %d is %lu\n", curr->pid, cputime);
            curr->cpu_time = cputime;
        } else {
            // remove the process from linkelist
            printk(KERN_INFO "remove pid%d from reg process list", curr->pid);
	        list_del(&curr->list);
            kfree(curr);	
	    }
        // curr->cpu_time++;
    }
    mutex_unlock(&lock);
}

static void queue_work_bottom_half(struct work_struct *p) {
    printk(KERN_INFO "bottom half called");
    update_process_cpu_time();
}

void _periodic_timer_callback(unsigned long p) {
    INIT_WORK((struct work_struct*)&process_work, queue_work_bottom_half);
    schedule_work(&process_work);
    //reset timer
    if (mod_timer(&periodic_timer, jiffies + msecs_to_jiffies(TIMER_DELAY))) {
        printk(KERN_INFO "error in mod_timer");
    }
}

*/
/*
 * this funciton first checks if workq is initialized, if not
 * create a workq and schedule a job
 **/
static int schedule_workqueue_job(void) {
    int retval = 0;
    if (!workq) {
        // if queue null, create the work queue before firing jobs on it
        workq = create_workqueue("MP3_WORKQUEUE");
        if (!workq) {
            retval = -ENOMEM;
            goto exit;
        }
    }
    INIT_DELAYED_WORK((struct delayed_work*)&mp3_delayed_work, work_callback);
    queue_delayed_work(workq, (struct delayed_work*)&mp3_delayed_work, msecs_to_jiffies(WORK_DELAY));
exit:
    return retval;
}

static void delete_workq(void) {
    if (workq) {
        printk(KERN_INFO "delete workq");
        if (cancel_delayed_work((struct delayed_work*)&mp3_delayed_work) == 0) {
            flush_workqueue(workq);
        }
        destroy_workqueue(workq);
        workq = NULL;
    }
}

static void work_callback(void* data) {
    printk(KERN_INFO "work_callback called!\n");
    collect_profile_sample();
    schedule_workqueue_job();
}

static struct file_operations mp3_fops = {
    .read = status_read,
    .write = status_write,
};

static int __init mp3_module_init(void)
{
    int retval = 0;
    printk(KERN_INFO "Module init called\n");
    mp3_root=proc_mkdir("mp3", NULL);
    if (mp3_root == NULL) {
        retval = -ENOMEM;
        goto exit;
    }
    mp3_status = proc_create("status", 0666, mp3_root, &mp3_fops);
    if (mp3_status == NULL) {
        retval = -ENOMEM;
        goto exit;
    }

    samples_buffer = (unsigned long*)vmalloc(NUM_PAGES * PAGE_SIZE);
    if (!samples_buffer) {
        printk(KERN_ALERT "samples buffer alloc failed!\n");
        retval = -ENOMEM;
        goto exit;
    }
    // set the buffer to zero
    memset (samples_buffer, 0, NUM_PAGES * PAGE_SIZE);
    // write pointer in the cyclic quue
    samples_buffer_write_index = 0;
    // count of the global sample number
    sample_num = 0;
    printk(KERN_INFO "ulong size=%lu", sizeof(unsigned long));
    max_samples = (NUM_PAGES * PAGE_SIZE) / sizeof(unsigned long);
    printk(KERN_INFO "max_samples:%lu\n", max_samples);
    retval = alloc_cdevice_num();
    if (retval != 0) {
        printk(KERN_INFO "failed to alloc major device num\n");
        if (samples_buffer) {
            vfree(samples_buffer);
            samples_buffer = NULL;
        }
        goto exit; 
    }
    int maj_dev_num =  MAJOR(mp3_dev_num);
    int min_dev_num =  MINOR(mp3_dev_num);
    printk(KERN_INFO "major=%d, minor=%d\n", maj_dev_num, min_dev_num);
    retval = add_cdevice_obj();
    if (retval != 0) {
        printk(KERN_INFO "failed to alloc device object\n");
        if (samples_buffer) {
            vfree(samples_buffer);
            samples_buffer = NULL;
        }
        goto exit; 
    }
exit:
    return retval;
}


static void __exit mp3_module_exit(void)
{
    printk(KERN_INFO "Module exit called\n");
    remove_proc_entry("status", mp3_root);
    remove_proc_entry("mp3",NULL);
    delete_workq();
    delete_process_list();
    free_cdevice_num();
    remove_cdevice_obj();
    if (samples_buffer) {
        vfree(samples_buffer);
    }
}

module_init(mp3_module_init);
module_exit(mp3_module_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("rychwdh2, srajend2, sharma55");
