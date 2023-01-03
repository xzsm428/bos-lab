### 一、运行环境

Ubuntu16.04 32位操作系统

x86架构

### 二、预备知识

每个进程都有它自己的虚拟地址空间，由MMU提供从虚拟地址空间到物理地址空间的转换，该空间的布局如下：

#### 进程的虚拟内存地址布局

![img](https://images0.cnblogs.com/blog2015/743874/201505/251606486466813.png)

对用户来说，主要关注的空间是User Space。将User Space放大后，可以看到里面主要分为如下几段：

- **Code**：这是整个用户空间的最低地址部分，存放的是指令（也就是程序所编译成的可执行机器码）
- **Data**：这里存放的是初始化过的全局变量
- **BSS**：这里存放的是未初始化的全局变量
- **Heap**：堆，这是我们重点关注的地方，堆自低地址向高地址增长，后面要讲到的brk相关的系统调用就是从这里分配内存
- **Mapping Area**：这里是与mmap系统调用相关的区域。大多数实际的malloc实现会考虑通过mmap分配较大块的内存区域，这个区域自高地址向低地址增长
- **Stack**：这是栈区域，自高地址向低地址增长

下面我们主要关注Heap区域的操作。

#### Heap内存模型

一般来说，malloc所申请的内存主要从Heap区域分配（本lab中不考虑通过mmap申请大块内存的情况）。

由上文知道，进程所面对的虚拟内存地址空间，只有按页映射到物理内存地址，才能真正使用。受物理存储容量限制，整个堆虚拟内存空间不可能全部映射到实际的物理内存。Linux对堆的管理示意如下：

![img](https://upload-images.jianshu.io/upload_images/619906-63ad829d2268dd99.png?imageMogr2/auto-orient/strip|imageView2/2/format/webp)

就虚拟地址而言，堆是一个连续的内存空间，它有三个划分的边界：起始点、最大值和一个`break`指针指向的终点。

##### break指针

Linux维护一个break指针，这个指针指向堆空间的某个地址。从堆起始地址到break之间的地址空间为映射好的，可以供进程访问；而从break往上，是未映射的地址空间，如果访问这段空间则程序会报错。

为了能够实现动态内存分配器，我们需要知道Heap's Start和break的位置。当然我们还需要有能力去移动break，可以使用`brk`和`sbrk`系统调用来实现。两个系统调用的原型如下：

```c
int brk(void *addr);
void *sbrk(intptr_t increment);
```

brk将break指针直接设置为传入的参数addr的地址，**而sbrk将break从当前位置移动increment所指定的增量（开辟新的内存空间的原理）。**brk在执行成功时返回0，否则返回-1并设置errno为ENOMEM；sbrk成功时返回break移动之前所指向的地址，否则返回(void *)-1。

一个小技巧是，**如果将increment设置为0，则可以获得当前break的地址。 当我们未移动过break指针时，我们也可以利用它来获取堆的开始位置。**

另外需要注意的是，由于Linux是按页进行内存映射的，如果我们设置的break没有和页边界对应，但系统实际上会在最后映射一个完整的页，从而实际已映射的内存空间比break指向的地方要大一些。虽然多出来的这段空间是可用的，但问题在于我们没有下一页边界的任何头绪，因此使用这段空间很容易出现问题。

##### rlimit

系统对每一个进程所分配的资源不是无限的，包括可映射的内存空间，因此每个进程有一个`rlimit`表示当前进程可用的资源上限。可以调用`<sys/resource.h>`里面的`setrlimit`和`getrlimit`对其进行管理。

每种资源有**软限制**和**硬限制**，并且可以通过setrlimit对rlimit进行有条件设置。其中硬限制作为软限制的上限，非特权进程只能设置软限制，且不能超过硬限制。

```c
//rlimit的结构体定义
struct rlimit {
    rlim_t rlim_cur;  /* Soft limit */
    rlim_t rlim_max;  /* Hard limit (ceiling for rlim_cur) */
};
```

### 三、实验内容

#### 数据结构

将堆内存空间以块（Block）的形式组织起来，每个块由<u>meta-data</u>和<u>数据区</u>组成，meta-data记录数据区的信息（数据区大小、空闲标志位、指针等等），数据区是真实分配的内存区域，并且数据区的第一个字节地址即为malloc返回的地址。

```c
typedef struct s_block *t_block; //meta-data
struct s_block
{
    size_t size;
    t_block next; //后继
    t_block prev; //前驱
    int free;     //1空闲 0不空闲
    void *ptr;    //指向数据块
    char data[1]; //指向meta-data尾部，用于分割meta-data和数据块，便于操作
};
```

#### 指针对齐

在前面介绍break指针时提到了指针对齐的重要性。由于本lab的运行环境是32位，所以指针是4的倍数（32bit=4Byte）。

定义meta-data块的大小<u>（尾部用于分割meta-data和数据区的1字节指针不计入）</u>：

```c
#define meta_data_size 20 //size of meta-data,不包括尾部用于分割的char data[1]
```

因此我们的meta-data已对齐（20是4的倍数），我们仅仅需要做的只是去对齐数据块的大小。

由数学推导可知，(x-1)/4*4+4的结果是最接近并且大于或者等于4的倍数。用移位操作符来提高运算速度，指针对齐操作可以定义如下：

```c
#define align4(x) (((((x)-1) >> 2) << 2) + 4) //用于将指针和整型大小对齐，meta-data已对齐，所以只需对齐数据块大小
```

#### malloc函数

##### 寻找block

定义全局指针base,指向Heap的起始位置：

```c
void *base = NULL; //指向堆的起始位置
```

现在考虑如何在block链中查找合适的block。本lab采用**first fit算法**，即从头开始查找，使用第一个数据区大小大于要求size的块作为此次分配的块，在函数`find_block`中实现：

```c
//返回一个合适的块，或者返回NULL（在没有找到的情况下）
//last保存上一次遍历过的块。当没有找到合适的块的时候，malloc函数可以通过它很轻松地去扩展堆的尾部（长度）
t_block find_block(t_block *last, size_t size)
{
    t_block b = base; //全局指针变量base指向堆的开始位置
    while (b && !(b->free && b->size >= size))//空闲块并且块大小>=需要的size
    {
        *last = b; //last指针指向上一次访问过的块
        b = b->next;//遍历
    }
    return b;
}
```

##### 开辟block

如果现有block都不能满足size的要求，则需要在链表最后开辟一个新的block。在函数`extend_heap`中实现：

```C
t_block extend_heap(t_block last, size_t size)
{
    t_block b;
    b = sbrk(0);//获取break指针地址
    if ((void *)-1 == sbrk(meta_data_size + size)) // meta_data_size+size=meta-data+数据块的内存大小
        return NULL; // sbrk失败,返回NULL
    b->size = size;
    b->next = NULL;
    b->prev = last;
    b->ptr = b->data;
    if (last) //不是第一个block
        last->next = b;//更新前驱的next指针
    b->free = 0;
    return b;
}
```

##### 拆分block

First fit有一个比较致命的缺点，就是可能会让很小的size占据很大的一块block，此时，为了提高payload，应该在剩余数据区足够大的情况下(>=4Byte)，将其分裂为一个新的block，如下图所示：

![img](https://upload-images.jianshu.io/upload_images/619906-1583453fd147db99.png?imageMogr2/auto-orient/strip|imageView2/2/format/webp)

这部分在函数`split_block`中进行实现：

```C
// 参数s必须要对齐
// 把旧的b分割成b+new
void split_block(t_block b, size_t s)
{
    if (!b)
        return;
    t_block new;
    new = (t_block)(b->data + s);
    new->size = b->size - s - meta_data_size;
    new->free = 1;
    new->next = b->next;
    new->prev = b;
    new->ptr = new->data;
    b->size = s;
    b->next = new;
    if (new->next)
        new->next->prev = new;
}
```

##### malloc实现

有了上面的代码铺垫，现在我们可以利用它们编写一个malloc函数了，它主要是将前面提到的函数封装起来。大致流程为：

- 首先对requested size进行对齐

- 判断base指针是否被初始化

  - 若被初始化，则说明不是第一个申请动态内存的

    - 调用`find_block`函数寻找合适的block

      - 若找到：

        1. 若块大小>=对齐后的requested size大小+meta-data size+4,则调用`split_block`函数对其进行拆分
        2. 标记该块为非空闲（free=0)

      - 若没找到：

        调用`extend_heap`函数开辟新的block。由于在`find_block`中`last`指针已经保存了block链中的最后一个block的地址，所以在拓展block的时候无需再重新遍历整个链表

  - 若未被初始化，则直接调用`extend_heap`拓展新的block

若extend_heap失败，则打出“run out of memory"的log。

在函数`malloc`中进行实现：

```c
void *malloc(size_t size)
{
    t_block last, b;
    size_t align_size = align4(size); //对齐请求数据块的大小
    if (base)
    {
        //base已初始化，说明不是第一个申请
        last = base;
        if ((b = find_block(&last, align_size)))
        {
            if (b->size - align_size >= (meta_data_size + 4))
                // 当一个块足够宽到请求的大小加上一个新块大小（至少meta_data_size+4），那么向链表中插入一个新块
                split_block(b, align_size);
            b->free = 0;
        }
        else
        {
            //查找heap失败，extend heap
            b = extend_heap(last, align_size);
            if (!b)
            {
                printf("run out of memory\n");
                return NULL; //extend失败
            }
        }
    }
    else
    {
        //首次调用malloc函数，extend heap
        b = extend_heap(base, align_size);
        if (!b)
        {
            printf("run out of memory\n");
            return NULL; //extend失败
        }
        base = b;
    }
    return b->data;
}
```

#### free函数

free函数的实现要解决两个关键问题：

1. 如何验证所传入的地址是有效地址，即确实是通过malloc方式分配的数据区首地址。
2. 如何解决碎片问题。

##### 验证有效地址

首先我们要保证传入free的地址是有效的，这个有效包括两方面：

- 地址应该在之前malloc所分配的区域内，即在base和当前break指针范围内
- 这个地址确实是之前通过我们自己的malloc分配的

第一个方面比较好解决，只需进行地址比较即可。

第二个方面的判定方法是：在meta-data的结构体内增加一个magic pointer `void *ptr`，这个指针指向数据区的第一个字节（也就是在合法时free时传入的地址），当`b->ptr==b->data`时，判定通过。

验证有效地址的代码实现如下：

```c
t_block get_block(void *p)//获取数据块的首地址
{ 
    char *tmp;
    tmp = p;
    return (p = tmp -= meta_data_size);
}

int vaild_addr(void *p)
{
    if (base)
        if (p > base && p < sbrk(0))//地址应该在之前malloc所分配的区域内，即在first_block和当前break指针范围内   
            return p == (get_block(p)->ptr); //ptr指向data域，如果b->ptr == b->data，那么free的地址大概率valid
    return 0; //invalid address
}
```

##### 碎片问题

当多次malloc和free后，整个内存池可能会产生很多碎片block，这些block很小，经常无法使用，甚至出现许多碎片连在一起的情况，虽然总体能满足某次malloc的要求，但是由于分割成了多个小block而无法fit，这就是碎片问题。

一个简单的解决方式是当free某个block时，如果发现它相邻的block也是free的，则将block和相邻block合并。在函数`fusion`中实现：

```c
//合并块
t_block fusion(t_block b)
{
    if (b->next && b->next->free)
    {
        b->size += meta_data_size + b->next->size; //前后两个数据块大小和meta_data_size大小加一起
        b->next = b->next->next;
        if (b->next) //如果后继的后继存在
            b->next->prev = b;
    }
    return b;
}
```

##### free实现

在解决了上述两个问题后，free函数的实现思路就比较清晰了。大致流程如下：

首先检查参数地址的合法性

- 非法，则不进行任何操作
- 合法
  - 获取block的地址并标记为空闲状态
  - 尝试合并前驱block
  - 是否是最后一个block
    - 否，尝试合并后继block
    - 是，回退break指针释放内存。判断当前block是否有前驱
      - 有，将前驱block的next设为NULL
      - 无，说明block链中没有节点，重置Heap's start(base=NULL)

代码实现如下：

```C
void free(void *ptr)
{
    t_block b;
    if (vaild_addr(ptr))
    {                       //是valid address
        b = get_block(ptr); //获取数据块的地址
        b->free = 1;        //标记为空闲状态
        // 尝试合并前驱块
        if (b->prev && b->prev->free)
            b = fusion(b->prev);
        //后继
        if (b->next)//当前块不是最后一个块          
            fusion(b); //尝试合并后继块
        else
        { //是最后一个块，直接释放内存
            if (b->prev) //有前驱块，将前驱块的next设为null
                b->prev->next = NULL;
            else
                base = NULL; //只剩它一个块了，直接重置base指针
            brk(b); //调整break指针到当前块位置
        }
    }
    //不是valid address则什么也不做
}
```

#### realloc函数

##### 内存复制

为了实现realloc，我们首先要实现一个内存复制方法。为了效率，我们以4字节为单位进行复制。在`copy_block`函数中实现：

```c
//内存拷贝
void copy_block(t_block src, t_block dst)
{
    int *sdata;
    int *ddata;
    size_t i;
    sdata = src->ptr;
    ddata = dst->ptr;
    //以4字节为单位进行复制
    for (i = 0; src->size > 4 * i && dst->size > 4 * i; i++)
        ddata[i] = sdata[i];
}
```

##### realloc实现

然后我们开始实现realloc。最简单的方法是malloc一段内存，然后将数据复制过去，释放旧内存中的数据后返回指向新内存地址处的指针。但是我们可以做的更高效，具体考虑以下几个方面的优化：

- 如果当前block的数据区略大于（<meta-data size+4)或等于realloc所要求的size，则不做任何操作
- 如果当前block的数据区远大于（>=meta-data size+4)realloc所要求的size，则进行block的拆分
- 如果当前block的数据区大小不能满足size
  - 其后继block是free的，并且合并后可以满足，则考虑做合并。若合并后空间很大的话则进行一次拆分
  - 合并不能解决，则用malloc分配新的block，复制数据后释放旧内存中的数据，最后返回指向新内存地址处的指针

代码实现如下：

```c
void *realloc(void *p, size_t size)
{
    if (p == NULL)
        //根据标准库文档，当p传入NULL时，相当于调用malloc
        return malloc(size);
    size_t s;
    t_block b, new;
    void *newp;
    if (vaild_addr(p))
    {
        s = align4(size);
        b = get_block(p);
        if (b->size >= s)
        {
            if (b->size >= s + meta_data_size + 4) //空闲块很大，全分给它浪费，可拆分
                split_block(b, s);
        }
        else
        {
            if (b->next && b->next->free && (b->next->size + b->size + meta_data_size) >= s)
            {
                //当前块空间不够但后继块空闲，并且两者合并后空间足够，则对其合并
                fusion(b);
                if (b->size - s >= (meta_data_size + 4)) //合并后空间很大，全给它浪费，拆分
                    split_block(b, s);
            }
            else
            {
                newp = malloc(s); //用malloc根据指定大小分配一个新块
                if (!newp)        //分配失败
                    return NULL;
                new = get_block(newp);
                copy_block(b, new); //将数据从旧内存地址复制到新内存地址处
                free(p);            //释放旧内存中的数据
                return newp;        //返回指向新内存地址处的指针
            }
        }
        return p;
    }
    return NULL; //invalid address
}
```

#### memory_init函数

在前文中的“Heap内存模型”部分提到系统对每一个进程所分配的资源不是无限的，包括可映射的内存空间，因此每个进程有一个`rlimit`表示当前进程可用的资源上限。可以调用`<sys/resource.h>`里面的`setrlimit`和`getrlimit`对其进行管理。

我们可以通过它来对操作系统分给每个进程的虚拟内存大小进行限制。因此我编写了`memory_init`函数用于设置用户态内存库向操作系统申请的虚拟内存大小，传入参数为要设置的虚拟内存上限`limit`。

```c
//设置虚拟内存大小上限
void memory_init(int limit){
    //打印当前的虚拟内存限制
    struct rlimit old_limit;
    getrlimit(RLIMIT_AS,&old_limit);
    printf("the process's old soft virtual memory limit: 0x%lx MB\n",old_limit.rlim_cur/1024/1024);
    printf("the process's old hard virtual memory limit: 0x%lx MB\n",old_limit.rlim_max/1024/1024);
    printf("\n");
    // 设置当前进程的虚拟内存大小限制为参数limit
    struct rlimit new_limit;
    new_limit.rlim_cur = limit;
    new_limit.rlim_max = limit;
    setrlimit(RLIMIT_AS,&new_limit);
    getrlimit(RLIMIT_AS,&new_limit);
    printf("the process's new soft virtual memory limit: %ld KB\n",new_limit.rlim_cur/1024);
    printf("the process's new hard virtual memory limit: %ld KB\n",new_limit.rlim_max/1024);
    printf("\n");
}
```

#### 测试程序

要求：

1. 在lab1生成的一个线程中申请一块内存区域，并free掉。注意，此时只要求单线程。
2. 用户态内存库向操作系统申请128KB大小的内存，在用户态线程中用循环依次申请1，2，4，8，16...字节大小的内存，直到没有空余内存，内存库应该处理这种情况，并打出“run out of memory”的log。

在main函数中创建了两个线程，线程函数分别为`test1`和`test2`，分别对应要求1和要求2。

##### test1

malloc一块内存，存入字符串”test of malloc"，并打印出存入的字符串和申请的内存空间的首地址。接着realloc该地址，并将保存的字符串修改为"test of realloc"，并打印出存入的字符串和申请的内存空间的首地址。最后free掉该内存空间。

```c
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
```

运行结果：

![image](https://user-images.githubusercontent.com/109144528/210307032-3473e137-6998-4295-a49d-53649254dc72.png)

可以看到线程正常调度，三个api:malloc，realloc和free均正常运行。

##### test2

调用`memory_init`函数向操作系统申请128KB大小的虚拟内存，并用循环依次申请1，2，4，8，16...字节大小的内存。若申请成功，打印“success"的log;若没有空余内存，打印”run out of memory"的log。

```C
void test2()
{
    memory_init(128*1024);//设置内存空间上限为128KB
    for (int i=1;;i*=2){
        printf("malloc:%dB---",i);
        if (malloc(i)==NULL) break;
        printf("success\n");
    }
}
```

运行结果：

![image](https://user-images.githubusercontent.com/109144528/210307051-a4a8ffdf-048b-4808-8849-34943f40ee7b.png)

### 四、代码模块

- **main.c**
  - test1
  - test2
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

### 五、实验总结

在这次实验中，我用C语言编写了一套动态内存分配器，实现了用户态内存管理的基本机制。在这个过程中我遇到了许多困难，查阅了不少博客与文章。虽然一边学习一边探索一边实践的过程比较艰辛，但我也从中收获了许多。不仅对C语言用户态内存库函数的底层原理有了更深入的了解，而且对计组和操作系统课上提到的虚拟内存相关知识有了更清晰的领会，受益匪浅。
