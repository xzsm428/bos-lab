#include <stdio.h>
#include <stdlib.h>
#include "thread.h"
#include <sys/time.h>

extern struct task_struct *current;
struct timeval start,end;
void schedule();
void fun1() {
    int count1=0;
    while (1){
    count1++;
    gettimeofday(&end, NULL);
    long long time=((end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec -start.tv_usec))/1000; // get the run time by microsecond
    printf("time: %lldms id: %d,Hello I'm thread1,my state is RUNNING. count1: %d\n",time,current->id,count1);
    if (count1==5) break;
    else mysleep(2);
    }
}

void fun2() {
    int count2=0;
    while(1){
    count2++;
    gettimeofday(&end, NULL);
    long long time=((end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec -start.tv_usec))/1000; // get the run time by microsecond
    printf("time: %lldms id: %d,Hello I'm thread2,my state is RUNNING. count2: %d\n",time,current->id,count2);
    if (count2==5) break;
    else mysleep(4);
   }
}

void fun3() {
    int count3=0;
    while(1){
    count3++;
    gettimeofday(&end, NULL);
    long long time=((end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec -start.tv_usec))/1000; // get the run time by microsecond
    printf("time: %lldms id: %d,Hello I'm thread3,my state is RUNNING. count3: %d\n",time,current->id,count3);
    if (count3==5) break;
    else mysleep(2);
    }
}

int main() {
  int tid1, tid2, tid3;
  printf("Hello, I'm main\n");
  
  gettimeofday(&start, NULL);
  thread_create(&tid1, fun1);
  printf("create thread %d\n", tid1);
  thread_create(&tid2, fun2);
  printf("create thread %d\n", tid2);
  thread_create(&tid3, fun3);
  printf("create thread %d\n", tid3);
  printf("\n");
  thread_join(tid1);
  thread_join(tid2);
  thread_join(tid3);
  
  return 0;
}


