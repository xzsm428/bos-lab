#ifndef __THREAD_H__
#define __THREAD_H__

#define MAX_NUMS 16 //最大线程个数
#define STACK_SIZE 1024 // 1024*4 B

// 线程状态
#define THREAD_RUNNABLE 0 //就绪态
#define THREAD_SLEEP 1 //阻塞态
#define THREAD_EXIT 2 //结束态
#define THREAD_RUNNING 3//运行态

struct task_struct {
  int id; //线程id
  void (*th_fn)(); //线程过程函数
  int esp; //  esp 栈顶指针
  int stack[STACK_SIZE]; //线程运行栈 
  unsigned int wakeuptime;//线程唤醒时间
  int status;//线程状态
};


int thread_create(int *tid, void (*start_routine)());
int thread_join(int tid);
void mysleep(int seconds);
#endif //__THREAD_H__
