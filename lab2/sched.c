#include "thread.h"
#include <stdlib.h>
#include <sys/time.h>
#include<stdio.h>
char* State[4]={"THREAD_RUNNABLE","THREAD_SLEEP","THREAD_EXIT","THREAD_RUNNING"};

extern struct task_struct *current;
extern struct task_struct *task[MAX_NUMS];

//swith.s 线程切换
void switch_to(struct task_struct *next);

// getmstime 用来取得毫秒精度时间
static unsigned int getmstime() {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) < 0) {
    perror("gettimeofday");
    exit(-1);
  }
  return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static struct task_struct *pick() {
  int current_id  = current->id;
  int i;

  struct task_struct *next = NULL;

repeat:
  for (i = 0; i < MAX_NUMS; ++i) {
    if (task[i] && task[i]->status == THREAD_SLEEP) {
      if (getmstime() > task[i]->wakeuptime)
        task[i]->status = THREAD_RUNNABLE;
    }
  }

  i = current_id;
  
  while(1) {
    i = (i + 1) % MAX_NUMS;
    if (i == current_id) {
      // 循环了一圈说明没找到可被调度的线程，重新计算线程是否可被唤醒。
      goto repeat;
    }
    if (task[i] && task[i]->status == THREAD_RUNNABLE) {
      next = task[i];
      break;
    }
  } 
  
  return next;
}



void schedule() {
    printf("schedule:old thread id:%d[%s]------>",current->id,State[current->status]);
    struct task_struct *next = pick();
    if (next) {
      printf("new thread id:%d[%s]\n\n",next->id,State[next->status]);
      if (next->id) next->status=THREAD_RUNNING;
      switch_to(next);
    }
}

/*假设线程需要休眠 10s，在调用 mysleep 的时候，我们计算出 10s 后的时间点，
该时间称为唤醒时间，然后保存到线程结构体中，再把该线程状态置于休眠状态，接着进入 schedule 函数。*/
void mysleep(int seconds) {
  //设计当前进程的唤醒时间
  current->wakeuptime = getmstime() + 1000*seconds;
  printf("Now I'm going to SLEEP,I will be RUNNABLE after %d seconds\n\n",seconds);
  // 将当前线程标记为休眠状态
  current->status = THREAD_SLEEP;
  // 调度
  schedule();
}

