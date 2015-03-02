clude <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/kthread.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group-10");
MODULE_DESCRIPTION("CS-423 MP2");

#define DEBUG 1
#define MP2PROC "mp2"
#define MP2STATUSPROC "status"
#define MP2_MAX_PROC_BUFFER_SIZE 1024
#define MP2_TASK_SLAB "mp2_cache"
#define MP2_SLEEPING 1
#define MP2_RUNNING 2
#define MP2_READY 3
#define MP2_DISPATCH_THREAD "mp2_dispatch_thread"

static struct proc_dir_entry* mp2_proc_dir;
static struct proc_dir_entry* mp2_status_file;

static char mp2_proc_buffer[MP2_MAX_PROC_BUFFER_SIZE];
static unsigned long mp2_proc_buffer_size = 0;

struct task_struct* find_task_by_pid(unsigned int nr) {
    struct task_struct* task;
    rcu_read_lock();
    task=pid_task(find_vpid(nr), PIDTYPE_PID);
    rcu_read_unlock();

    return task;
}

struct mp2_task_struct {
	struct task_struct* linux_task;
	struct timer_list wakeup_timer;
	unsigned int period;
	unsigned int computation_time;
	unsigned int task_state;
	unsigned int pid;
	// unsigned long relative_period;
	// unsigned long slice;
	unsigned int next_period;
};

// linked list to hold pids of different processes
struct mp2_proc_list {
    unsigned int pid;
    unsigned int period;
    unsigned int computation_time;
    struct mp2_task_struct* mp2_task;
    struct list_head klist;
};

struct mp2_proc_list mp2_processes_list;
struct mp2_task_struct* mp2_current_task;

static DEFINE_MUTEX(mp2_mutex_lock);

static struct kmem_cache* mp2_cache;

static struct task_struct* mp2_kdthread; 

//static struct timer_list mp2_proc_timer;

static int mp2_dispatcher(void *data) {
	struct mp2_task_struct* min_period_task;	
    struct mp2_proc_list* tmp;
    struct list_head *pos;
    struct sched_param sparam;
	int min_period;
	
	while (!kthread_should_stop()) {
		printk(KERN_INFO "In kernel thread\n");

		// do context switching here
		// find task with ready state and highest priority.
		// preempt the current task (if any) and context switch
		// if no task ready then only preempt
		// old task state change to ready if only running
		// for new task change state to running

		min_period = -1;
		min_period_task = NULL;

		mutex_lock(&mp2_mutex_lock);
		list_for_each(pos, &mp2_processes_list.klist) {
        	tmp = list_entry(pos, struct mp2_proc_list, klist);
	   	    if (tmp->mp2_task->task_state == MP2_READY && (min_period == -1 || tmp->mp2_task->period < min_period)) {
	        	min_period = tmp->mp2_task->period;
	        	min_period_task = tmp->mp2_task;
	        }
	    }
	    
	    if (min_period != -1 && min_period_task != NULL)
	    	printk(KERN_INFO "Min Period is %d and task pid is %d", min_period, min_period_task->pid);
	    else
	    	printk(KERN_INFO "No task is ready yet\n");


		// for old task
		if (mp2_current_task != NULL) {
			sparam.sched_priority=0;
			printk(KERN_INFO "Changing pid %d to ready", mp2_current_task->pid);
			if (mp2_current_task->linux_task != NULL)  {
				sched_setscheduler(mp2_current_task->linux_task, SCHED_NORMAL, &sparam);
				printk(KERN_INFO "Switched pid %d successfully\n", mp2_current_task->pid);
			}
			if (mp2_current_task->task_state == MP2_RUNNING)
				mp2_current_task->task_state = MP2_READY;
		}

		// for new task
		if (min_period != -1 && min_period_task != NULL) {
			printk(KERN_INFO "Changing pid %d to running", min_period_task->pid);
			wake_up_process(min_period_task->linux_task);
			set_task_state(min_period_task->linux_task, TASK_RUNNING);
			sparam.sched_priority=99;
	  		sched_setscheduler(min_period_task->linux_task, SCHED_FIFO, &sparam);
	  		min_period_task->task_state = MP2_RUNNING;
	  		mp2_current_task = min_period_task;
		}

	    mutex_unlock(&mp2_mutex_lock);   

		// maintain a global variable of currently running task
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);
	
	do_exit(0);
	return 0;
}

static void mp2_task_timer_callback(unsigned long pid_data) {
    struct mp2_proc_list* tmp;
    struct list_head *pos;
    unsigned int pid = (unsigned int) pid_data;

    //int ret;

    printk(KERN_INFO "mp2_task_timer_callback called (%ld) and data is %d\n", jiffies, pid);
    // change task to ready
    // wakeup dispatching thread
	
	mutex_lock(&mp2_mutex_lock);
    list_for_each(pos, &mp2_processes_list.klist) {
        tmp = list_entry(pos, struct mp2_proc_list, klist);
        if (tmp->mp2_task->pid == pid) {
        	tmp->mp2_task->task_state = MP2_READY;
        }
    }
    mutex_unlock(&mp2_mutex_lock);   

	if (mp2_kdthread) {
		wake_up_process(mp2_kdthread);
	} else {
		printk(KERN_ALERT "No running thread\n");
	}
}

static void mp2_proc_yield(unsigned int pid) {
    struct mp2_proc_list* tmp;
    struct list_head *pos;
    int ret;
	
	printk(KERN_INFO "In process deregistration function\n");
    
    
	printk(KERN_INFO "In process yield function\n");
	// sleep the associated task and setup the timer
	// timer callback change the task to ready state and wake the dispatching thread
	// set_task_state(struct task_struct*,TASK_UNINTERRUPTIBLE) 
	// calculate next release time and set the timer. don't set it negative
	
    mutex_lock(&mp2_mutex_lock);
    list_for_each(pos, &mp2_processes_list.klist) {
        tmp = list_entry(pos, struct mp2_proc_list, klist);
        if (tmp->mp2_task->pid == pid) {
        	if (tmp->mp2_task->next_period == 0) {
        		tmp->mp2_task->next_period = jiffies + msecs_to_jiffies(tmp->mp2_task->period);
        	} else {
        		tmp->mp2_task->next_period = msecs_to_jiffies(tmp->mp2_task->next_period) + msecs_to_jiffies(tmp->mp2_task->period) - jiffies;
        		if (tmp->mp2_task->next_period <= 0)
        			tmp->mp2_task->next_period = jiffies + msecs_to_jiffies(tmp->mp2_task->period);
        	}
        	
        	tmp->mp2_task->task_state = MP2_SLEEPING;
        	set_task_state(tmp->mp2_task->linux_task, TASK_UNINTERRUPTIBLE);
        	printk(KERN_INFO "Putting %d pid task to sleep\n", pid);

        	ret = mod_timer(&tmp->mp2_task->wakeup_timer, tmp->mp2_task->next_period);
        	if (ret) printk(KERN_ALERT "Error in mod_timer\n");
        }
    }
    mutex_unlock(&mp2_mutex_lock);   

	if (mp2_kdthread) {
		wake_up_process(mp2_kdthread);
	} else {
		printk(KERN_ALERT "No running thread\n");
	}
}

static void mp2_process_add(unsigned int pid, unsigned int period, unsigned int computation_time, struct mp2_task_struct* mp2_task) {
    struct mp2_proc_list* tmp;
    
    tmp  = (struct mp2_proc_list*) kmalloc(sizeof(struct mp2_proc_list), GFP_ATOMIC);
    tmp->pid = pid;
    tmp->period = period;
    tmp->computation_time = computation_time;
    tmp->mp2_task = mp2_task;

    mutex_lock(&mp2_mutex_lock);
    list_add_tail(&(tmp->klist), &(mp2_processes_list.klist));
    mutex_unlock(&mp2_mutex_lock);
}
static int isAdmissible(unsigned int period, unsigned int computation_time) {
	struct mp2_proc_list* tmp;
    struct list_head *pos;
    unsigned long computation_sum = (unsigned long) computation_time, period_sum = (unsigned long) period;

    mutex_lock(&mp2_mutex_lock);
    list_for_each(pos, &mp2_processes_list.klist) {
        tmp = list_entry(pos, struct mp2_proc_list, klist);
    	period_sum += (unsigned long) tmp->mp2_task->period;
    	computation_sum += (unsigned long) tmp->mp2_task->computation_time;
    }
    mutex_unlock(&mp2_mutex_lock); 

    if (computation_sum > period_sum)
    	return -1;
    
    if ((computation_sum * 1000) <= (period_sum * 693))
    	return 0;
    return -1;
}

static void mp2_proc_register(unsigned int pid, unsigned int period, unsigned int computation_time) {
	struct mp2_task_struct* mp2_new_task;

	printk(KERN_INFO "In registration message with %d pid, %d period, %d computation_time", pid, period, computation_time);

	// register only if admissible
	if (isAdmissible(period, computation_time) != 0) {
		return;
	}

	mp2_new_task = (struct mp2_task_struct*) kmem_cache_alloc(mp2_cache, GFP_ATOMIC);
	mp2_new_task->linux_task = find_task_by_pid(pid);
	mp2_new_task->pid = pid;
	mp2_new_task->period = period;
	mp2_new_task->computation_time = computation_time;
	mp2_new_task->task_state = MP2_SLEEPING;
	mp2_new_task->next_period = 0;
	setup_timer(&mp2_new_task->wakeup_timer, mp2_task_timer_callback, (unsigned long) pid);

	mp2_process_add(pid, period, computation_time, mp2_new_task);
}

static void mp2_proc_deregister(unsigned int pid) {
    struct mp2_proc_list* tmp;
    struct list_head *pos, *q;
	
	printk(KERN_INFO "In process deregistration function\n");
    
    mutex_lock(&mp2_mutex_lock);
    list_for_each_safe(pos, q, &mp2_processes_list.klist) {
        tmp = list_entry(pos, struct mp2_proc_list, klist);
        if (tmp->mp2_task->pid == pid) {
        	printk(KERN_INFO "Freeing %d\n", tmp->mp2_task->pid);
        	if (mp2_current_task != NULL && tmp->mp2_task->pid == mp2_current_task->pid) mp2_current_task = NULL;

        	list_del(pos);
        	del_timer_sync(&tmp->mp2_task->wakeup_timer);
        	kmem_cache_free(mp2_cache, tmp->mp2_task);
        	kfree(tmp);
        }
    }
    mutex_unlock(&mp2_mutex_lock);   
}

static ssize_t mp2_proc_write(struct file *file, const char* buffer, unsigned long count, void *data) {
    int i, j, k;
    unsigned long pid, period, computation_time;
    char* msg_content[4];
    int msg_size;

    mp2_proc_buffer_size = count;

    if (mp2_proc_buffer_size > MP2_MAX_PROC_BUFFER_SIZE) {
        mp2_proc_buffer_size = MP2_MAX_PROC_BUFFER_SIZE;
    }

    if (mp2_proc_buffer_size <= 0) return mp2_proc_buffer_size;
    // write data to buffer

    if (copy_from_user(mp2_proc_buffer, buffer, mp2_proc_buffer_size)) {
        return -EFAULT;
    }

    for (i = 0; i < mp2_proc_buffer_size; i++) {
    	printk("%c", mp2_proc_buffer[i]);
    }

    i = 0;
    msg_size = 0;
    while (i < mp2_proc_buffer_size && msg_size < 4) {
    	if (mp2_proc_buffer[i] == ' ') {
    		i++;
    		continue;
    	}
    	j = 0;
    	while (i < mp2_proc_buffer_size && mp2_proc_buffer[i] != ',' && mp2_proc_buffer[i] != '\n' && mp2_proc_buffer[i] != '\0' && mp2_proc_buffer[i] != ' ') {
    		j++;
    		i++;
    	}
    	//printk("Size is %d", j);
    	msg_content[msg_size] = (char*) kmalloc(sizeof(char) * (j + 1), GFP_ATOMIC);
    	for (k = 0; k < j; k++) {
    		msg_content[msg_size][j - k - 1] = mp2_proc_buffer[i - k - 1];
    		//printk("%c", msg_content[msg_size][j - k - 1]);
    	}
    	msg_content[msg_size][j] = '\0';
    	//printk("\n");
    	msg_size++;
    	i++;
    }
    
    // handle different type of messages
    //R, PID, PERIOD, COMPUTATION
    //Y, PID
    //D, PID

    //////////////////////////////////
    if (msg_size == 4 && strcmp(msg_content[0], "R") == 0) {
    	printk(KERN_INFO "This is a registration message\n");
		if (kstrtol(msg_content[1], 10, &pid)) printk(KERN_ALERT "Not a valid number as pid");
		else if (kstrtol(msg_content[2], 10, &period)) printk(KERN_ALERT "Not a valid number as period");
		else if (kstrtol(msg_content[3], 10, &computation_time)) printk(KERN_ALERT "Not a valid number as computation_time");
		else {
			mp2_proc_register((unsigned int) pid, (unsigned int) period, (unsigned int) computation_time);
		}
    } else if (msg_size == 2) {
    	if (strcmp(msg_content[0], "Y") == 0) {
    		printk(KERN_INFO "This is a yield message\n");
    		if (kstrtol(msg_content[1], 10, &pid)) printk(KERN_ALERT "Not a valid number as pid");
    		else {
    			//mp2_process_add((unsigned int) pid, 0, 0);
    			//yield function call	
    			mp2_proc_yield((unsigned int) pid);	
    		}
    	} else if (strcmp(msg_content[0], "D") == 0) {
    		printk(KERN_INFO "This is a deregistration message\n");
    		if (kstrtol(msg_content[1], 10, &pid)) printk(KERN_ALERT "Not a valid number as pid");
    		else {
    			//mp2_process_add((unsigned int) pid, 0, 0);
    			// de registration function call
    			mp2_proc_deregister((unsigned int) pid);
    		}
    	} else {
    		printk(KERN_ALERT "This is an invalid message\n");
    	}
    } else {
    	printk(KERN_ALERT "This is an invalid message\n");
    }


    for (i = 0; i < msg_size; i++) {
    	//printk("J => %s \n", msg_content[i]);
    	kfree(msg_content[i]);
    }

    printk(KERN_INFO "\n");

    return mp2_proc_buffer_size;
}

// called at begining of sequence
static void *mp2_seq_start(struct seq_file *s, loff_t *pos) {
    static unsigned long counter = 0;
    if (*pos == 0) {
        return &counter; // begin sequence
    } else {
        *pos = 0; // end of sequence, return null and stop reading
        return NULL;
    }
}

// called after begining of sequence until end of sequence
static void *mp2_seq_next(struct seq_file *s, void *v, loff_t *pos) {
    unsigned long *tmp_v = (unsigned long *)v;
    (*tmp_v)++;
    (*pos)++;
    return NULL;
}

// called at end of sequence
static void mp2_seq_stop(struct seq_file *s, void *v) {
    ;//nothing to do since all static in start;
}

// called after each step of sequence 
static int mp2_seq_show(struct seq_file *s, void *v) {
    struct mp2_proc_list* tmp;
    struct list_head *pos;

    // loff_t *spos = (loff_t *) v;
    // seq_printf(s, "%Ld times called. Now printing list of pids\n", *spos);

    mutex_lock(&mp2_mutex_lock);
    list_for_each(pos, &mp2_processes_list.klist) {
        tmp = list_entry(pos, struct mp2_proc_list, klist);
        seq_printf(s, "PID => %d, PERIOD => %d msecs, COMPUTATION_TIME => %d msecs\n", tmp->pid, tmp->period, tmp->computation_time);
    }
    mutex_unlock(&mp2_mutex_lock);

    return 0;
}

// structure to gather function to manage the sequence
static struct seq_operations mp2_proc_seq_ops = {
    .start = mp2_seq_start,
    .next  = mp2_seq_next,
    .stop  = mp2_seq_stop,
    .show  = mp2_seq_show,
};

static int mp2_proc_seq_open(struct inode *inode, struct file *file) {
    return seq_open(file, &mp2_proc_seq_ops);
}

static const struct file_operations mp2_proc_fops = {
    .owner   = THIS_MODULE,
    .open    = mp2_proc_seq_open,
    .read    = seq_read,
    .write   = mp2_proc_write,
    .llseek  = seq_lseek,
    .release = seq_release,
};

int __init mp2_init(void) {
    
    int ret = 0;
#ifdef DEBUG
    printk(KERN_INFO "MP2 Module Loading\n");
#endif
    printk(KERN_INFO "Hello World\n");
    
    // making proc dir and status file

    mp2_proc_dir = proc_mkdir_mode(MP2PROC, 0, NULL);
    if (mp2_proc_dir == NULL) {
        ret = -ENOMEM;
    } else {
        mp2_status_file = proc_create(MP2STATUSPROC, 0666, mp2_proc_dir, &mp2_proc_fops);
        if (mp2_status_file == NULL) {
            ret = -ENOMEM;
        }
    }
	
    // initialize linked list
    INIT_LIST_HEAD(&mp2_processes_list.klist);

    // initialize slab allocater
    // can sleep
	mp2_cache = kmem_cache_create(MP2_TASK_SLAB, sizeof(struct mp2_task_struct), 0, 0, NULL);
	if (mp2_cache == NULL) {
		printk(KERN_ALERT "Cannot assign slab allocater.\n");
		ret = -ENOMEM;
	}

	// create and run a dispatcher thread
	// mp2_kdthread = kthread_create(mp2_dispatcher, NULL, MP2_DISPATCH_THREAD);
	mp2_kdthread = kthread_run(mp2_dispatcher, NULL, MP2_DISPATCH_THREAD);
	
	if (mp2_kdthread == NULL) {
		printk(KERN_ALERT "Unable to create thread\n");
		ret = -ENOMEM;
	}
	printk(KERN_INFO "MP2 Module Loaded\n");

    return ret;
}

void __exit mp2_exit(void) {
	int ret;
    struct mp2_proc_list* tmp;	
    struct list_head *pos, *q;
    
#ifdef DEBUG
    printk(KERN_INFO "MP2 Module UnLoading\n");
#endif
    printk(KERN_INFO "GoodBye World!\n");

    // delete proc dirs
    remove_proc_entry(MP2STATUSPROC, mp2_proc_dir);
    remove_proc_entry(MP2PROC, NULL);

    // delete list 
    printk(KERN_INFO "Deleting pid from the list\n");

    mutex_lock(&mp2_mutex_lock);
    list_for_each_safe(pos, q, &mp2_processes_list.klist) {
        tmp = list_entry(pos, struct mp2_proc_list, klist);
        printk(KERN_INFO "Freeing %d\n", tmp->pid);
        list_del(pos);
        del_timer_sync(&tmp->mp2_task->wakeup_timer);
        kmem_cache_free(mp2_cache, tmp->mp2_task);
        kfree(tmp);
    }
    mutex_unlock(&mp2_mutex_lock);
    

	mutex_lock(&mp2_mutex_lock);    
    // delete all objects before destorying it
    // free slab cache
    // must be synchronized
    kmem_cache_destroy(mp2_cache);
    
	mutex_unlock(&mp2_mutex_lock);

    mutex_destroy(&mp2_mutex_lock);

    // stopping kernel thread;
    printk(KERN_INFO "Stopping kernel thread\n");
	ret = kthread_stop(mp2_kdthread);
	if (ret) printk(KERN_ALERT "Unable to stop thread.\n");

	printk(KERN_INFO "MP2 Module Unloaded\n");

}

module_init(mp2_init);
module_exit(mp2_exit);
