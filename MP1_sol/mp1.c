/*  
 *  hello-2.c - Demonstrating the module_init() and module_exit() macros.
 *  This is preferred over using init_module() and cleanup_module().
 */
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
#include "mp1_given.h"

#define TIMER_DELAY 5000
static DEFINE_MUTEX(lock);
static LIST_HEAD(plist_head);
static struct timer_list periodic_timer ;
struct work_struct process_work;
typedef struct _process_meta_node {
    int pid;
    unsigned long cpu_time;
    struct list_head list;
} process_meta_node;

static struct proc_dir_entry *mp1_root;
static struct proc_dir_entry *mp1_status;
ssize_t status_read(struct file *filp, char __user *buff,
    size_t count, loff_t *offp);
ssize_t status_write(struct file *filp, const char __user *buff,
    size_t count, loff_t *offp);
int is_pid_registered(long int target_pid);
int delete_process_list(void);
int register_process(long int pid);
int get_process_info_list_as_string(char *op);
void periodic_timer_callback(unsigned long);
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

void periodic_timer_callback(unsigned long p) {
    INIT_WORK((struct work_struct*)&process_work, queue_work_bottom_half);
    schedule_work(&process_work);
    //reset timer
    if (mod_timer(&periodic_timer, jiffies + msecs_to_jiffies(TIMER_DELAY))) {
        printk(KERN_INFO "error in mod_timer");
    }
}

int get_process_info_list_as_string(char *buff) {
    int size = 0;
    process_meta_node *curr;
    mutex_lock(&lock);
    list_for_each_entry(curr, &plist_head, list) {
        size += sprintf(buff, "%s %d: %lu\n", buff, curr->pid, curr->cpu_time);
    }
    mutex_unlock(&lock);
    return size; 
}

int register_process(long int pid) {
    int retval = 0;
    process_meta_node *pnode;
    pnode = (process_meta_node*)kmalloc(sizeof(process_meta_node), GFP_KERNEL);
    if (pnode == NULL) return -ENOMEM;
    pnode->pid = pid;
    pnode->cpu_time = 0;
    
    mutex_lock(&lock);
    if(is_pid_registered(pid)) {
        printk(KERN_INFO "register_process::pid %ld already registerd", pid);
        retval = -EFAULT;
        goto exit;
    }
    // added to head to save some time. order not important
    list_add(&pnode->list, &plist_head);
    printk(KERN_INFO "register_process::pid %ld added", pid);
    retval = 0; 
exit:
    mutex_unlock(&lock);
    return retval;
}

int is_pid_registered(long int target_pid) {
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

int delete_process_list() {
    process_meta_node *curr, *temp;
    mutex_lock(&lock);
    list_for_each_entry_safe(curr, temp, &plist_head, list) {
        list_del(&curr->list);
        kfree(curr); 
    }
    mutex_unlock(&lock);
    return 0;
}

ssize_t status_read(struct file *filp, char __user *buff,
    size_t count, loff_t *offp)  {
    static int finished = 0;
    int retval = 0;

    if(finished) {
        printk(KERN_INFO "proc read finished");
        finished = 0;
        retval = 0;
        goto exit;
    }

    finished  = 1;
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
    long int pid = 0; 
    if (kstrtol(input, 10, &pid) != 0) {
        retval = -EINVAL;
        goto exit;
    }
    retval = register_process(pid);
    if (retval != 0) {
        goto exit;
    }
    //pid read successful
    retval = count;
    printk(KERN_INFO "pid  %ld registered", pid);
exit:
    if (input) {
        kfree(input);
    }    
    return retval;
}

static struct file_operations mp1_fops = {
    .read = status_read,
    .write = status_write,
};

static int __init mp1_module_init(void)
{
    int retval = 0;
    printk(KERN_INFO "Hello, world 2\n");
    mp1_root=proc_mkdir("mp1", NULL);
    if (mp1_root == NULL) {
        retval = -ENOMEM;
        goto exit;
    }
    mp1_status = proc_create("status", 0666 /*S_IFREG | S_IRUGO | S_IWUGO*/, mp1_root, &mp1_fops);
    if (mp1_status == NULL) {
        retval = -ENOMEM;
        goto exit;
    }
    
    setup_timer(&periodic_timer, periodic_timer_callback, 0);
    // 5 sec time interval for wakepup
    retval = mod_timer(&periodic_timer, jiffies + msecs_to_jiffies(TIMER_DELAY));
    if (retval) {
        printk(KERN_INFO "error in mod_timer");
        goto exit;
    }
exit:
    return retval;
}

static void __exit mp1_module_exit(void)
{
    printk(KERN_INFO "Goodbye, world 2\n");
    remove_proc_entry("status", mp1_root);
    remove_proc_entry("mp1",NULL);
    del_timer(&periodic_timer);
    flush_scheduled_work();
    delete_process_list();
}

module_init(mp1_module_init);
module_exit(mp1_module_exit);
MODULE_LICENSE("GPL");
