#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

#define RUNTIME 5
#define MAX_N 1000
#define MAX_CHARS_PER_LINE 100
#define MAX_LIFE 3
#define FACTOR 5

int mp2_read_status(int id);
int mp2_register(int id, int period, int computation_time);
int mp2_yield(int id);
int mp2_deregister(int id);
void factorial_calculations(int run_time);
void mywork();

int main(int argc, char* argv[])
{
    int period = 40;
    int computation_time = 20;
    int ret = 0;
    struct timeval start, end;
    double t1, t2;
    
    int id = getpid();
    printf("My pid is %d\n", id);

    int arr_length = sizeof(argv) / sizeof(char*);
    if (argc < 3) {
        printf("Argument not sufficient. Exiting...\n"); 
        return -5;
    }
    period = atoi(argv[1]);
    computation_time = atoi(argv[2]);

    printf("Arguments are %d, %d\n", period, computation_time); 
    // register pid with the kernel mp1 module
    ret = mp2_register(id, period, computation_time);
    if (ret != 0) {
        printf("Please check if /mp2/proc/status file is existing\n. Exiting....\n");
        return ret;
    }

    // read the user time values
    ret = mp2_read_status(id);
    if (ret != 0) {
        printf("Process you are trying to submit will voilate the rate monotonic scheduler admission control.\n");
        //printf("Please check if /mp2/proc/status file is existing\n");
        printf("ID was not registered. Exiting...\n");
        return ret;
    }

    // yield function call
    ret = mp2_yield(id);
    if (ret != 0) {
        printf("Please check if /mp2/proc/status file is existing\n. Exiting....\n");
        return ret;
    }    
    // run some periodic task
    gettimeofday(&start, NULL);
    
    printf("Now running some tasks\n");
    int i = 0;
    while (i < MAX_LIFE) {
        factorial_calculations(computation_time * FACTOR);
        ret = mp2_yield(id);
        if (ret != 0) {
            printf("Please check if /mp2/proc/status file is existing\n. Exiting....\n");
            return ret;
        }
        i++;
    }

    // Deregister pid from the scheduler
    ret = mp2_deregister(id);
    if (ret != 0) {
        printf("Please check if /mp2/proc/status file is existing\n. Exiting....\n");
        return ret;
    }
    gettimeofday(&end, NULL);
    t1 = start.tv_sec + (start.tv_usec/1000000.0);  
    t2 = end.tv_sec+(end.tv_usec/1000000.0);  
    printf("%.6lf seconds elapsed\n", (t2 - t1));

    //printf("Ran successfully for %ld\n", ((end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec)));
    
	return 0;
}

int mp2_read_status(int id) {
    FILE* PRfptr;
    ssize_t len = 0;
    ssize_t read;
    char *line = NULL;
    int contains_pid = -1;
    char pid_line[16];
    
    PRfptr = fopen("/proc/mp2/status", "r");
    if (PRfptr == NULL) {
        printf("/proc/mp2/status file is not present\n");
        return -1;
    }
    
    sprintf(pid_line, "PID => %d", id);

    while ((read = getline(&line, &len, PRfptr)) != -1) {
        //printf("Length of Line is %zu : \n", read);
        if (strstr(line, pid_line) != NULL) {
            contains_pid = 0;
        }
    }
    if (line)
        free(line);
    fclose(PRfptr);

    if (contains_pid == -1) {
        return -1;
    }
    printf("PID Registered\n");
    
    return 0;
}

int mp2_register(int id, int period, int computation_time) {
    FILE* PWfptr = fopen("/proc/mp2/status", "w");
    if (PWfptr == NULL) {
        printf("/proc/mp2/status file is not present\n");
        return -1;
    }
    // Register R, pid, period, processing time
    fprintf(PWfptr, "R, %d, %d, %d", id, period, computation_time);
    fclose(PWfptr);
    
    return 0;
}

int mp2_yield(int id) {
    FILE* PWfptr = fopen("/proc/mp2/status", "w");
    if (PWfptr == NULL) {
        printf("/proc/mp2/status file is not present\n");
        return -1;
    }
    // Yield Y, pid
    fprintf(PWfptr, "Y, %d", id);
    fclose(PWfptr);
    return 0;
}

int mp2_deregister(int id) {
    FILE* PWfptr = fopen("/proc/mp2/status", "w");
    if (PWfptr == NULL) {
        printf("/proc/mp2/status file is not present\n");
        return -1;
    }
    // DeRegister D, pid
    fprintf(PWfptr, "D, %d", id);
    fclose(PWfptr);
    printf("PID DeRegistered\n");

    return 0;
}

// takes around 1.5s on my machine
void mywork() {
    int c = 1, d = 1;
    int x = 10000; //~1/5sec ~ 200ms
    for ( c = 1 ; c <= x; c++ )
        for ( d = 1 ; d <= x; d++ )
            {}
}

void factorial_calculations(int run_time) {
    int i;
    long int result;
    int timer = 1;
    int c = 1, d= 1;

    for (i = 1; i < MAX_N && timer < run_time; i++, timer++) {
        result *= i;
        mywork();//1.5s
    }
    return;
}