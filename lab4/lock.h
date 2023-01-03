#ifndef __LOCK_H__
#define __LOCK_H__
typedef struct mutex_lock{
    int flag; //对临界区的锁
    int guard; //保护队列和flag的自旋锁
    int count; //记录线程数量
}mutex_lock;

void mutex_acquire(mutex_lock *m);
void mutex_release(mutex_lock *m);
void spinlock_acquire();
void spinlock_release();
void put(int value);
int get();
void *producer();
void *consumer(void *arg);
#endif