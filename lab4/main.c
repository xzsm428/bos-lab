#include "thread.h"
#include "memory.h"
#include "lock.h"
#include <stdio.h>
#include <sys/time.h>
#include<pthread.h>
extern struct task_struct *current;
struct timeval start,end;
void *test_spinlock(void* value){//value为写入的值
    spinlock_acquire();//获取锁
    int *ptr=(int*)malloc(1024);//申请1024字节大小内存
    int num=1024/sizeof(int);//向内存中填入int,共可填入256个
    for (int i=0;i<num;i++){
        ptr[i]=*((int*)value);
        printf("%d ",ptr[i]);

    }
    printf("\n");
    printf("----------\n");
    free(ptr);
    spinlock_release();//释放锁
}

mutex_lock m;
void *test_mutex(void* value){//value为写入的值
    mutex_acquire(&m);//获取锁
    int *ptr=(int*)malloc(1024);//申请1024字节大小内存
    int num=1024/sizeof(int);//向内存中填入int,共可填入256个
    for (int i=0;i<num;i++){
        ptr[i]=*((int*)value);
        printf("%d ",ptr[i]);
    }
    printf("\n");
    printf("----------\n");
    free(ptr);
    mutex_release(&m);//释放锁
}

void test_cv(){
    pthread_t PID1,PID2,PID3;
    int arg1=1,arg2=2;
    pthread_create(&PID1,NULL,producer,NULL);
    pthread_create(&PID2,NULL,consumer,(void*)&arg1);
    pthread_create(&PID3,NULL,consumer,(void*)&arg2);
    pthread_join(PID1,NULL);
    pthread_join(PID2,NULL);
    pthread_join(PID3,NULL);
}

int main(){
    int arr[200];//数组arr中存了0~199的值，用于向内存中写入数据
    for (int i=0;i<200;i++)
        arr[i]=i;
    for (int i=1;i<=2;i++){
        printf("------test spinlock——round%d:------\n",i);
        pthread_t PID[200];
        for (int j=0;j<200;j++){
            pthread_create(&PID[j],NULL,test_spinlock,(void *)&arr[j]);
        }
        for (int j=0;j<200;j++){
            pthread_join(PID[j],NULL);
        }
    }
    printf("\n\n");
    for (int i=1;i<=2;i++){
        printf("------test mutex——round%d:------\n",i);
        pthread_t PID[200];
        for (int j=0;j<200;j++){
            pthread_create(&PID[j],NULL,test_mutex,(void *)&arr[j]);
        }
        for (int j=0;j<200;j++){
            pthread_join(PID[j],NULL);
        }
    }
    printf("------test condition variable------\n");
    test_cv();
    return 0;
}


