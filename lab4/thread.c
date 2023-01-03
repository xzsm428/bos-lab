#include "thread.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

void schedule();

extern struct timeval start,end;

static struct task_struct init_task = {0, THREAD_RUNNABLE, 0, {0}};

struct task_struct *current = &init_task;

struct task_struct *task[MAX_NUMS] = {&init_task,};

// 线程启动函数
static void Start(struct task_struct *tsk) {
  tsk->th_fn();
  tsk->status = THREAD_EXIT;
  gettimeofday(&end, NULL);
  long long time=((end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec -start.tv_usec))/1000; // get the run time by microsecond
  printf("time: %lldms THREAD EXIT id:%d \n\n",time,current->id);
  schedule();
}

//tid 是传出参数，start_routine 是线程过程函数
int thread_create(int *tid, void (*start_routine)()) {
  int id = -1;
  // 为线程分配一个结构体
  struct task_struct *tsk = (struct task_struct*)malloc(sizeof(struct task_struct));
  // 在任务槽中寻找一个空位置
  while(++id < MAX_NUMS && task[id]);
  // 如果没找到就返回 -1
  if (id == MAX_NUMS) return -1;
  // 将线程结构体指针放到空的任务槽中
  task[id] = tsk;
  // 将任务槽的索引号当作线程 id 号，传回到 tid
  if (tid) *tid = id;
  // 初始化线程结构体
  tsk->id = id;
  tsk->th_fn = start_routine;
  int *stack = tsk->stack; // 栈顶界限
  tsk->esp = (int)(stack+STACK_SIZE-11);
  tsk->wakeuptime = 0;
  tsk->status = THREAD_RUNNABLE;
   
  // 初始 switch_to 函数栈帧
  stack[STACK_SIZE-11] = 7; // eflags
  stack[STACK_SIZE-10] = 6; // eax
  stack[STACK_SIZE-9] = 5; // edx
  stack[STACK_SIZE-8] = 4; // ecx
  stack[STACK_SIZE-7] = 3; // ebx
  stack[STACK_SIZE-6] = 2; // esi
  stack[STACK_SIZE-5] = 1; // edi
  stack[STACK_SIZE-4] = 0; // old ebp
  stack[STACK_SIZE-3] = (int)Start; // ret to start
  // start 函数栈帧，刚进入 start 函数的样子 
  stack[STACK_SIZE-2] = 100;// ret to unknown，如果 start 执行结束，表明线程结束
  stack[STACK_SIZE-1] = (int)tsk; // start 的参数

  return 0;
}

int thread_join(int tid) {
  while(task[tid]->status != THREAD_EXIT) {
    schedule();
  }
  gettimeofday(&end, NULL);
  long long time=((end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec -start.tv_usec))/1000; // get the run time by microsecond
  printf("time: %lldms free the memory of thread id:%d\n\n",time,tid);
  free(task[tid]); //清理执行完的线程所占用的空间
  task[tid] = NULL;
}
