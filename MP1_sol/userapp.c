#include "userapp.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>


long int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n-1);
}

int main(int argc, char* argv[])
{
    pid_t id = getpid();
	char cmd[100];
    sprintf(cmd, "sudo echo %d >/proc/mp1/status", id);
    int ret = system(cmd);
    if (ret != 0) {
        printf("failed to run cmd");
        return -1;
    }
    int n = 10;
    int count = 0;
    while (count++ < 10000000000) {
        fact(n);
        //sleep(2);
        //printf("fact of %d is %ld\n", n, fact(n));
    }
    return 0;
}
