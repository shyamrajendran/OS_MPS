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
//#include "mp2_given.h"

//task state
#define READY 0
#define RUNNING 1
#define SLEEPING 2

static DEFINE_MUTEX(lock);
static LIST_HEAD(plist_head);
static struct proc_dir_entry *mp2_root;
static struct proc_dir_entry *mp2_status;
static struct kmem_cache* pcb_cache;
typedef struct mp2_task_struct {
    int pid;//process id
    long period;//period
    long computation;//processing time
    long state;//task state
    struct task_struct* linux_task;
    struct timer_list wakeup_timer;
    struct list_head list;
} mp2_task_struct;

ssize_t status_read(struct file *filp, char __user *buff,
    size_t count, loff_t *offp);
ssize_t status_write(struct file *filp, const char __user *buff,
    size_t count, loff_t *offp);
ssize_t handleMessgae(char type, int pid, long period, long computation);
int register_process(int pid, long period, long computation);
int deregister_process(int pid);
int _is_pid_registered(int target_pid);
int get_process_info_list_as_string(char *buff);
int delete_process_list(void);

int delete_process_list() {
    mp2_task_struct *curr, *temp;
    mutex_lock(&lock);
    list_for_each_entry_safe(curr, temp, &plist_head, list) {
        list_del(&curr->list);
        kmem_cache_free(pcb_cache, (void*)curr);
    }
    mutex_unlock(&lock);
    return 0;
}

int deregister_process(int pid) {
    mp2_task_struct *curr, *next;
    int retval = -EINVAL;
    printk("DEREGISTER PID:%d\n", pid);
    mutex_lock(&lock);
    list_for_each_entry_safe(curr, next, &plist_head, list) {
        if (curr->pid == pid) {
            //TBD:free other allocated resources
            list_del(&curr->list);
            kmem_cache_free(pcb_cache, (void*)curr);
            printk(KERN_INFO "process %d deregistered!\n", pid);
            retval = 0;
            goto exit;
        }
    }
exit:
    mutex_unlock(&lock);
    return retval;
}

int get_process_info_list_as_string(char *buff) {
    int size = 0;
    mp2_task_struct *curr;
    mutex_lock(&lock);
    list_for_each_entry(curr, &plist_head, list) {
        size += sprintf(buff, "%s %d %ld %ld\n", buff, curr->pid, curr->period, curr->computation);
    }
    mutex_unlock(&lock);
    return size;
}

int _is_pid_registered(int target_pid) {
    int retval = 0;
    mp2_task_struct *curr;
    list_for_each_entry(curr, &plist_head, list) {
        if (curr->pid == target_pid) {
            retval = 1;
            break;
        }
    }
    return retval;
}

int register_process(int pid, long period, long computation) {
    mp2_task_struct *pcb;
    int retval = 0;
    printk("REGISTER PID:%d, PERIOD:%ld, COMPUTATION:%ld\n", pid, period, computation);
    pcb = (mp2_task_struct*)kmem_cache_alloc(pcb_cache, GFP_KERNEL);
    if (pcb == NULL) {
        retval = -ENOMEM;
        goto exit;
    }
   
    pcb->pid = pid;
    pcb->period = period;
    pcb->computation = computation;
    pcb->state = SLEEPING;
    /*pcb->linux_task = find_task_by_pid(pid);
    if (pcb->linux_task == NULL) {
        retval = -ENOMEM;
        goto exit;
    }*/
 
    mutex_lock(&lock);
    if(_is_pid_registered(pid)) {
        printk(KERN_INFO "register_process::pid %d already registerd", pid);
        retval = -EFAULT;
        goto exit;
    }
    // added to head to save some time. order not important
    list_add(&pcb->list, &plist_head);
    printk(KERN_INFO "register_process::pid %d added", pid);
exit:
    mutex_unlock(&lock);
    return retval;
}
ssize_t handleMessgae(char type, int pid, long period, long computation) {
    int retval = 0;
    switch(type) {
        case 'R':
            return register_process(pid, period, computation);
            break;
        case 'Y':
            printk("YIELD PID:%d\n", pid);
            break;
        case 'D':
            deregister_process(pid);
            break;
        default:
            retval = -EINVAL;
    }
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
    char *p;
    char *space;
    long var = 0;
    long pid = 0;
    long period = 0;
    long computation = 0;

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
    //R pid period time
    p = input + 2;
    space = strchr(p, ' ');
    pid = -1;
    computation = -1;
    period = -1;
    while(space) {
        *(space) = 0;
        if (kstrtol(p, 10, &var) != 0) {
            printk(KERN_INFO "error in write");
            retval = -EINVAL;
            goto exit;
        }
        if (pid == -1) {
            pid = var;
        } else if (period == -1) {
            period = var;
        }
        p = space + 1;
        space = strchr(p, ' ');
    }

    //last token
    if (kstrtol(p, 10, &var) != 0) {
            printk(KERN_INFO "error in write\n");
            retval = -EINVAL;
            goto exit;
    }

    if (pid == -1 && (type == 'Y' || type == 'D')) {
        pid = var;
    } else if (type == 'R' && pid != -1 && period != -1 && computation == -1) {
        computation = var;
    } else {
        printk(KERN_INFO "error in write\n");
        retval = -EINVAL;
        goto exit;
    }

    //printk(KERN_INFO "parse write %c %ld %ld %ld",type, pid, period, computation);
    retval = handleMessgae(type, pid, period, computation);
    if (retval == 0) {
        // if success return the number of bytes read
        retval = count;
    }
exit:
    if (input) {
        kfree(input);
    }
    return retval;
}

static struct file_operations mp2_fops = { 
    .read = status_read,
    .write = status_write,
};

static int __init mp2_module_init(void)
{
    int retval = 0;
    printk(KERN_INFO "MP2\n");
    mp2_root=proc_mkdir("mp2", NULL);
    if (mp2_root == NULL) {
        retval = -ENOMEM;
        goto exit;
    }   
    mp2_status = proc_create("status", 0666, mp2_root, &mp2_fops);
    if (mp2_status == NULL) {
        retval = -ENOMEM;
        goto exit;
    }

    //create cache slab
    pcb_cache = kmem_cache_create("mp2_pcb_cache", sizeof(mp2_task_struct), 0, GFP_KERNEL ,NULL);
exit:
    return retval;
}

static void __exit mp2_module_exit(void)
{
    printk(KERN_INFO "Goodbye, MP2\n");
    remove_proc_entry("status", mp2_root);
    remove_proc_entry("mp2",NULL);
    delete_process_list();
}

module_init(mp2_module_init);
module_exit(mp2_module_exit);
