### 一、运行环境

Ubuntu16.04 32位操作系统

x86架构

### 二、实验内容

#### 1、数据结构定义

线程结构体设计

```c
#define STACK_SIZE 1024 // 1024*4 B
struct task_struct {
  int id; //线程id
  void (*th_fn)(); //线程过程函数
  int esp; //  esp 栈顶指针
  int stack[STACK_SIZE]; //线程运行栈 
  unsigned int wakeuptime;//线程唤醒时间
  int status;//线程状态
};
```

用数组task存放线程的结构体指针，用于线程的调度

```c
#define MAX_NUMS 16 // 最大线程个数

struct task_struct *task[MAX_NUMS]; // 用于存放线程的结构体指针
```

#### 2、上下文切换

汇编指令在执行的时候，最重要地方在于它需要依赖 CPU 环境（32位）：

- 一套通用寄存器 (eax、edx、ecx、ebx、esp、ebp、esi、edi)
- 标志寄存器 eflags
- 指令寄存器 eip (eip 用来保存下一条被指令的地址)  
- 栈（用于保存一些临时数据，比如某条指令的地址）

在通用寄存器中，有一个寄存器名为esp，它保存的是栈顶指针(内存地址的值)。指令 push、pop、call、ret 等都依赖于 esp 工作。

所谓的切换控制流，本质就是更改esp栈顶指针以达到切换栈的目的，同时修改通用寄存器的环境，以适应新指令的执行环境。

通常，这段新指令的执行环境，恰好也保存在栈里，就像下图中 esp 到“某条指令地址”之间那段内存的数据。
![image](https://images.gitbook.cn/eab44220-47b7-11e9-88c2-2b079160052b)

下面介绍上下文切换的具体过程，以线程0切换到线程1为例：

- 线程 0 正准备切换时，将当前 CPU 中的寄存器环境一个一个压入到自己的栈中，最后一个压栈的是 eflags 寄存器；
- 线程 0 将自己的结构体指针保存到全局数组 task[0] 中；
- 线程 0 从 task[1] 中取出线程 1 的结构体指针，将栈顶指针保存到 CPU 的 esp 寄存器中。此时意味着栈已经被切换。栈切换完成后，本质上已经在线程 1 中了；
- 线程 1 将自己栈中的寄存器环境 pop 到对应的 CPU 寄存器中，比如第一个 pop 到 eflags 中，最后一个是 pop ebp。

代码实现：

```assembly
/*void switch_to(struct task_struct *next)*/

.section .text
.global switch_to /* 导出函数switch_to */
switch_to:
  push %ebp
  mov %esp, %ebp /* 更改栈帧，以便寻参 */

  /* 保存现场 */
	push %edi
	push %esi
	push %ebx
	push %edx
	push %ecx
	push %eax
  pushfl
 
  /* 准备切换栈 */
  mov current, %eax /* 取 current 基址放到 eax */
  mov %esp, 8(%eax) /* 保存当前 esp 到线程结构体 */ 
  mov 8(%ebp), %eax /* 取下一个线程结构体基址*/
  mov %eax, current /* 更新 current */
  mov 8(%eax), %esp /* 切换到下一个线程的栈 */

  /* 恢复现场, 到这里，已经进入另一个线程环境了，本质是 esp 改变 */
  popfl
	popl %eax
	popl %edx
	popl %ecx
	popl %ebx
	popl %esi
	popl %edi
	popl %ebp
  ret

```

#### 3、用户态线程的机制

提供了两个对用户态线程的函数接口：thread_create和thread_join  

##### 3.1 thread_create

用户态线程的创建过程封装在函数thread_create中,代码说明详见注释。

```c
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
   
  /* 初始 switch_to 函数栈帧，
  寄存器中填充的0~7数字本身没有任何意义，单纯为了调试方便*/
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
```

注意到在stack[STACK_SIZE-3]即stack[1021]处填充了一个函数Start的指针，为什么要这样设计呢？让我们回到上下文切换部分的switch_to函数：

```assembly
  /* 恢复现场, 到这里，已经进入另一个线程环境了，本质是 esp 改变 */
  popfl
	popl %eax
	popl %edx
	popl %ecx
	popl %ebx
	popl %esi
	popl %edi
	popl %ebp
  ret
```

一连串的 pop 动作将栈中的值弹到 cpu 寄存器中。我在构造的时候，只是随便填了一些值，因为这并不会有任何影响。switch_to 函数执行到 ret 指令的时候，esp 这个时候指向的是 stack[1021] 这个位置，一旦 ret，就进入了 Start 函数。  
Start函数：

```c
// 线程启动函数
static void Start(struct task_struct *tsk) {
  tsk->th_fn();//执行线程函数
  tsk->status = THREAD_EXIT;//当线程函数执行完后，线程结束
  gettimeofday(&end, NULL);
  long long time=((end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec -start.tv_usec))/1000; // get the run time by microsecond
  printf("time: %lldms THREAD EXIT id:%d \n\n",time,current->id);
  schedule();
}
```

##### 3.2 thread_join

主要功能是调度和在线程结束后清理占用空间。

```c
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
```

#### 4、线程状态设计

设计了四种线程状态：就绪态、阻塞态、结束态、运行态。

```c
// 线程状态
#define THREAD_RUNNABLE 0 //就绪态
#define THREAD_SLEEP 1 //阻塞态
#define THREAD_EXIT 2 //结束态
#define THREAD_RUNNING 3//运行态
```

线程状态的切换将在之后的调度策略和测试程序部分进行介绍。

#### 5、调度策略

首先介绍mysleep函数，它的实现思路为：  
假设线程需要休眠 10s，在调用 mysleep 的时候，我们计算出 10s 后的时间点。比如调用 mysleep 的时间是 07:30:25，我们计算出 10s 后的时间是 07:30:35，该时间称为唤醒时间，然后保存到线程结构体中，再把该线程状态置于休眠状态，接着进入 schedule 函数。

```c
void mysleep(int seconds) {
  //设计当前进程的唤醒时间
  current->wakeuptime = getmstime() + 1000*seconds;
  printf("Now I'm going to SLEEP,I will be RUNNABLE after %d seconds\n\n",seconds);
  // 将当前线程标记为休眠状态
  current->status = THREAD_SLEEP;
  // 调度
  schedule();
}
```

用getmstime函数计算出wakeuptime：

```c
// getmstime 用来取得毫秒精度时间
static unsigned int getmstime() {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) < 0) {
    perror("gettimeofday");
    exit(-1);
  }
  return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
```

调度函数schedule：

```c
void schedule() {
    printf("schedule:old thread id:%d[%s]------>",current->id,State[current->status]);
    struct task_struct *next = pick();
    if (next) {
      printf("new thread id:%d[%s]\n\n",next->id,State[next->status]);
      if (next->id) next->status=THREAD_RUNNING;
      switch_to(next);
    }
}
```

其中调度算法主要封装在pick函数中，具体过程为：  
从当前线程为起点向后循环查找可调度的线程（状态为THREAD_RUNNABLE），若循环了一圈则说明没找到可被调度的线程，需重新计算线程是否可被唤醒（goto repeat）。  
计算过程为遍历task,用getmstime()获取当前时间并与处于阻塞态的每个线程的唤醒时间（task[i]->wakeuptime）相比较，若已超过唤醒时间，则将其状态切换为就绪态。  
具体代码实现如下：

```c
static struct task_struct *pick() {
  int current_id  = current->id;
  int i;

  struct task_struct *next = NULL;

//遍历task，将当前时间>wakeuptime的线程唤醒
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
```

#### 6、测试程序

在main函数中创建3个线程thread1~thread3并执行。

```c
  thread_create(&tid1, fun1);
  printf("create thread %d\n", tid1);
  thread_create(&tid2, fun2);
  printf("create thread %d\n", tid2);
  thread_create(&tid3, fun3);
  printf("create thread %d\n", tid3);
```

```c
  thread_join(tid1);
  thread_join(tid2);
  thread_join(tid3);
```

线程函数fun1~fun3大体相同，仅唤醒时间的设置有所差异，以fun1为例。具体为定义变量count1用于记录循环执行次数，若执行次数达到5则退出循环，否则将线程阻塞一段时间。

```c
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
```

运行结果（截取部分）：

![image](https://user-images.githubusercontent.com/109144528/210306774-bdb98323-30c5-43f3-a6ba-a5b7909f5cfb.png)

![image](https://user-images.githubusercontent.com/109144528/210306789-2949203b-405d-4ead-8b0c-b938d13fc424.png)

从输出的信息中可以看到切换的时机，切换前后线程的id以及两个线程的状态。可以看到线程按照设定的睡眠时间来执行，并且在count1~count3计数到5后，线程进入结束态，并最终释放占用空间。各功能点正常运行。

### 三、代码模块

- **main.c**
  - fun1
  - fun2
  - fun3
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

### 四、实验总结

在这次实验中，我用C语言和汇编语言实现了一套简易的用户态线程，对用户态线程的机制（包括线程结构体包含信息、线程状态、线程调度、线程栈和寄存器的关系、线程函数的执行、上下文切换等）有了更深入的了解，同时学习了x86架构下的基本汇编指令并自己动手用汇编语言编写了上下文切换的程序，加深了对课堂上所学知识的理解与运用，受益匪浅。

