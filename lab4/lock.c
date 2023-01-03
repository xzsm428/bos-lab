#include"lock.h"
#include"memory.h"
#include<stdio.h>
#include<pthread.h>
#include<assert.h>
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
int spinlock=0;
mutex_lock m;

void mutex_acquire(mutex_lock *m){
    while (__atomic_test_and_set(&m->guard,1) == 1);
    m->count++;//记录线程数量
    if (m->flag == 0) {//锁未被持有，直接获取锁
        m->flag = 1; 
        m->guard=0;
    } else { //锁已被持有
        //put thread on wait queue
        //thread state=waiting

        /************pthread_cond_wait()**********/
        pthread_mutex_lock(&mut); 
        m->guard=0;
        pthread_cond_wait(&cond, &mut); 
        pthread_mutex_unlock(&mut);

        //调度回来后该线程持有锁，因此flag依然为1
    }
}

void mutex_release(mutex_lock *m){
    while (__atomic_test_and_set(&m->guard,1) == 1);
    m->count--;//记录线程数量
    if (m->count==0){//wait queue为空
        m->flag=0;//没有线程需要，释放锁
    }
    else{//wait queue中有线程等待锁，唤醒
        //take thread of queue,put it on ready queue

        /************pthread_cond_signal()**********/
        pthread_mutex_lock(&mut); 
        pthread_cond_signal(&cond); 
        pthread_mutex_unlock(&mut);
    }
    m->guard=0;
}

void spinlock_acquire(){
    while (__atomic_test_and_set(&spinlock,1) == 1);
}

void spinlock_release(){
    spinlock=0;
}

pthread_cond_t empty = PTHREAD_COND_INITIALIZER;//唤醒producer的cv
pthread_cond_t fill = PTHREAD_COND_INITIALIZER;//唤醒consumer的cv
int flag=1; //1:buffer为空，0：buffer不为空
int buffer; //生产者和消费者写入的buffer
int end=0;//标记生产者是否还会在生产，会0,不会1

void put(int value) {//生产者向buffer中写数据
    assert(flag == 1);
    flag = 0;
    buffer = value;
}
int get() {//消费者从buffer中取数据
    assert(flag == 0);
    if (buffer==100000) end=1;//生产者不会再写数据
    else flag=1;
    return buffer;
}
void *producer() {
    for (int i = 1; i <= 100000; i++) {
        pthread_mutex_lock(&mut);
        while (flag == 0) //等待consumer读取
            pthread_cond_wait(&empty, &mut);
        put(i);
        printf("produce:%d\n",i);
        pthread_cond_signal(&fill);//唤醒consumer
        pthread_mutex_unlock(&mut);
    }
    //结束所有consumer
    pthread_mutex_lock(&mut);
    pthread_cond_broadcast(&fill);
    pthread_mutex_unlock(&mut);
    printf("producer exit\n");
}
void *consumer(void *arg) {
    int symbol=*((int*)arg);//consumer的标号
    while(!end) {
        pthread_mutex_lock(&mut);
        while (flag == 1) //等待producer写入
            pthread_cond_wait(&fill, &mut);
        if (!end){//生产没结束，可以取数据
        int tmp = get();
        printf("consumer%d:%d\n",symbol,tmp);
        }
        pthread_cond_signal(&empty);//唤醒producer
        pthread_mutex_unlock(&mut);
    }
    //唤醒producer来结束所有线程
    pthread_mutex_lock(&mut);
    pthread_cond_signal(&empty);
    pthread_mutex_unlock(&mut);    
    printf("consumer%d exit\n",symbol);
}