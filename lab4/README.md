## 一、运行环境

Ubuntu16.04 32位操作系统

x86架构

## 二、实验内容

### spinlock

所谓spinlock，指的是在任一时刻只有一个线程能够获得锁。当一个线程在获取锁的时候，如果锁已经被其它线程获取，那么该线程将循环等待，然后不断的判断锁是否能够被成功获取，直到获取到锁才会退出循环。

根据spinlock的原理，我们先尝试写出伪代码实现：

```c
//循环检查锁状态，并尝试获取，直到成功
while (true){
  locked = get_lock();
  if (locked == false){
    locked = true;
    break;
  }
}
//上锁后执行相关任务
do_something;

//执行完毕，释放锁
locked = false;
```

但上面的逻辑在并发场景会遇到问题：两个线程可能会同时进入`if`语句，同时获取锁，导致锁不生效。

解决这个问题的方法是将查询锁（get_lock）和设置锁（locked=true）组合成一个原子操作，保证同一时间只有一个线程在执行。伪代码如下所示：

```c
//这里get_and_set(locked)就是一个原子操作，执行成功后把locked设置成true。
while (get_and_set(locked) == false)
  continue;

do_something;

locked = false;
```

如此就实现了一个简单的自旋锁，我们可以通过`test&set`指令来实现`get_and_set(locked)`这个原子操作，并将获取锁和释放锁两个操作模块化为`spinlock_acquire()`和`spinlock_release()`两个函数，具体代码如下：

```c
int spinlock=0;//0表示锁被占用，1表示锁未被占用
```

#### spinlock_acquire()

```c
void spinlock_acquire(){
    while (__atomic_test_and_set(&spinlock,1) == 1);
}
```

#### spinlock_release()

```c
void spinlock_release(){
    spinlock=0;
}
```

### mutex

由于spinlock在自旋时不会让出CPU，因此会产生额外的开销，并且无法针对不同优先级的线程进行调度。而mutex锁可以很好的解决这些问题。

先来看看mutex的伪代码实现：

```c
int guard = 0;//自旋锁，使得对flag和线程队列的操作是原子的
int flag = FREE;//对临界区的锁
Acquire() {
    // Short busy-wait time
    while (test&set(guard));
    if (flag == BUSY) {//锁已被持有，线程进入wait queue
        put thread on wait queue;
        cur_thread->state = WAITING;
        thread_switch() & guard = 0;
    } else {//锁未被持有，直接获取锁
        flag = BUSY;
        guard = 0;
    }
}
Release() {
    // Short busy-wait time
    while (test&set(guard));
    if anyone on wait queue {//wait queue中有线程等待锁，唤醒
        take thread off wait queue
        Place on ready queue;
    } else {//没有线程等待锁，直接释放
        flag = FREE;
    }
    guard = 0;
}
```

从伪代码中可以看出，mutex锁中维护了一个等待队列，当一个线程持有锁时，其余线程会让出cpu并进入等待队列等待唤醒，而不是一直自旋占用cpu,并且通过队列的调度也可以实现线程优先级的区分。

但这其中并没有完全避免自旋，可以看到guard起到了自旋锁的作用，使得对flag和线程队列的操作是原子的。因此，线程在获取锁或者释放锁时可能被中断，从而导致其他线程自旋等待。但是，这个自旋等待时间是很有限的（不是用户定义的临界区，只是在 Acquire()和 Release() 中的一段代码），因此是完全可以接受的。

由于没有实现线程的tick调度，因此本lab采用pthread库中的condition variable的`wait()`和`signal()`操作实现对线程队列的组织。具体代码如下：

#### 结构体定义

```c
typedef struct mutex_lock{
    int flag; //对临界区的锁
    int guard; //保护队列和flag的自旋锁
    int count; //记录线程数量
}mutex_lock;
```

#### mutex_acquire()

```c
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
```

#### mutex_release()

```c
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
```

对于课上提到的signal如果先于wait发生，这个线程就会一睡不醒的问题。我的解决方案是wait和signal使用同一个pthread mutex `mut`,并且将`m->guard=0`这个操作和`wait()`用`mut`一块锁住，将guard解锁、线程状态置为waiting、放入wait queue和线程切换这些操作合并为一个原子操作，这样就不会产生问题了。

### 测试mutex/spinlock

#### 要求

与lab2配合。

在之前的lab2中，我们拥有了内存管理机制，但是当时我们不能支持多个线程同时使用这个内存分配器，现在我们可以拥有这个feature；现在我们可以用spinlock和mutex分别给内存分配器加上同步的机制，支持多个线程同时使用malloc；
    使用200个线程，每个线程分配1kB内存，在200个线程里面将申请到的内存每个字节依次填写为0-199（**这里我的理解是对于第x个创建的线程，将其申请的内存里填写为数字x,如对于线程1就将其申请的内存中填满数字1**)；
    最后检查这些内存区域是否和分配预取的一致，重复以上实验50次；
    注：如果你自己写的用户库没有实现lab3中的tick调度功能，需要使用pthread线程库来创建线程；

由于没有实现tick调度，因此使用pthread库来创建线程。

#### 测试spinlock

##### 代码

```c
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
```

```c
for (int i=1;i<=50;i++){
    printf("------test spinlock——round%d:------\n",i);
    pthread_t PID[200];
    for (int j=0;j<200;j++){
        pthread_create(&PID[j],NULL,test_spinlock,(void *)&arr[j]);//数组arr中存了0~199的值
    }
    for (int j=0;j<200;j++){
        pthread_join(PID[j],NULL);
    }
}
```

##### 结果 
![image](https://user-images.githubusercontent.com/109144528/210307400-2525d5aa-5b0d-4732-a4d2-118936e779b8.png)
![image](https://user-images.githubusercontent.com/109144528/210307416-a679ac67-723c-4197-86b5-fc0c327760e1.png)

从输出结果中可以看出每个线程都成功将申请到的内存填入正确的数据并输出，并且这个过程中没有发生线程的调度，spinlock成功地将其变为了原子操作，但由于没有队列的组织和调度，因此不存在线程的优先级先后之分，谁先抢到锁就是谁的，因此输出的各组数字比较随机，体现了线程调度的随机性和不确定性。

#### 测试mutex

##### 代码

```c
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
```

```c
for (int i=1;i<=50;i++){
    printf("------test mutex——round%d:------\n",i);
    pthread_t PID[200];
    for (int j=0;j<200;j++){
    	pthread_create(&PID[j],NULL,test_mutex,(void *)&arr[j]);//数组arr中存了0~199的值
    }
    for (int j=0;j<200;j++){
    	pthread_join(PID[j],NULL);
    }
}
```

##### 结果

![image](https://user-images.githubusercontent.com/109144528/210307434-202ee292-dafe-4c49-9144-5685661bff89.png)

![image](https://user-images.githubusercontent.com/109144528/210307446-782dc789-da5f-4e90-848d-d3e1587715c8.png)

从输出结果中可以看出每个线程都成功将申请到的内存填入正确的数据并输出，并且这个过程中没有发生线程的调度，mutex成功地将其变为了原子操作，并且由于存在队列的组织和调度，因此线程调度有优先级之分，因此输出的各组数字比较有顺序。

### condition variable

先来看伪代码实现：

```c
void wait(Lock *lock) {
    assert(lock.isHeld());
    waiting.add(myTCB); //加入到等待队列
    scheduler.suspend(&lock); // 切换到其它线程并且释放锁
    lock->acquire(); //此时线程调度回来，重新获取锁
}
void signal() {
    if (waiting.notEmpty()) {
        thread = waiting.remove();//从等待队列中移除
        scheduler.makeReady(thread);//使线程转换为就绪态
    }
}
void broadcast() {
    while (waiting.notEmpty()) {//while循环保证将等待队列中的所有线程唤醒
        thread = waiting.remove();//从等待队列中移除
        scheduler.makeReady(thread);//使线程转换为就绪态
    }
}
```

由于没有实现tick调度，因此队列的组织只能采用pthread库中condition variable的`wait(),signal(),broadcast()`接口实现。因此本lab中没有再单独构建cv的wait，signal，broadcast三个接口。

### 生产者——消费者模型

#### 要求

搭建一个消费生产者模型，生产者在每次被唤醒时向一个全局变量依次写入1-100000，两个消费者线程获取这个全局变量的值，并打印。

需要保证结果的正确性：在生产者写入和消费者读值后，需要对一个flag分别设0和1；在写入和读值前，需要在assert里面假设这个flag分别为1和0。

#### 实现

```c
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
```

代码实现如上，注释写得比较详细，这里就不再赘述。值得说明的一点是在循环结束后还需要再次发signal来结束所有线程：假设最后的数据由consumer1取走，取走后consumer1跳出循环，此时若不发signal唤醒producer,则producer和consumer2会一直等待signal而不会结束线程。因此需要最后取数的consumer唤醒producer，而producer再唤醒其余的consumer来使所有线程结束。

#### 结果
![image](https://user-images.githubusercontent.com/109144528/210307495-5787608a-dbae-48ce-8ecc-75df2aff19ae.png)

从输出结果中可以看出，消费者——生产者模型正常运行，producer和两个consumer顺序存取，没有产生consumer的争抢等问题。

## 三、代码模块

- **main.c**
  - test_spinlock
  - test_mutex
  - test_cv
  - main
- **sched.c**
  - getmstime
  - pick
  - schedule
  - mysleep
- **switch.s**
  - switch_to
- **thread.c**
  - Start
  - thread_create
  - thread_join
- **thread.h**
- **memory.c**
  - memory_init
  - find_block
  - extend_heap
  - split_block
  - malloc
  - fusion
  - get_block
  - valid_addr
  - free
  - copy_block
  - realloc

- **memory.h**

- **lock.c**
  - mutex_acquire
  - mutex_release
  - spinlock_acquire
  - spinlock_release
  - put
  - get
  - producer
  - consumer
- **lock.h**

## 四、实验总结

在这次实验中，我用C语言与pthread库配合实现了用户态的同步机制，对操作系统课上学习的test&set、spinlock、mutex和condition variable有了更深入的理解与感悟，受益匪浅。美中不足的是由于没有实现lab3中的tick调度，因此线程队列的组织和调度只能通过调pthread库的conidtion variable来实现，没能自己从底层出发一步步实现线程的队列调度。
