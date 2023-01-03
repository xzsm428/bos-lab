#include "thread.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include<string.h>

extern struct task_struct *current;
struct timeval start,end;
void test1()
{
    char *str;

    if (str = (char *)malloc(20))
    {
        strcpy(str, "test of malloc");
        printf("String = %s,  Address = %p\n", str, str);

        if (str = (char *)realloc(str, 25))
        {
            strcpy(str, "test of realloc");
            printf("String = %s,  Address = %p\n", str, str);
        }

        free(str);
    }
}
void test2()
{
    memory_init(128*1024);//设置内存空间上限为128KB
    for (int i=1;;i*=2){
        printf("malloc:%dB---",i);
        if (malloc(i)==NULL) break;
        printf("success\n");
    }
}

int main() {
    int tid1, tid2;
    gettimeofday(&start, NULL);
    thread_create(&tid1, test1);
    printf("create thread\n");
    printf("\n");
    thread_join(tid1);
    thread_create(&tid2, test2);
    printf("create thread\n");
    printf("\n");
    thread_join(tid2);
    return 0;
}


