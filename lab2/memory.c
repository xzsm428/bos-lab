#include "memory.h"
#include <stdio.h>
#include <unistd.h>
#include<string.h>
#include <sys/resource.h>

void *base = NULL; //指向堆的起始位置

//设置虚拟内存大小上限为128KB
void memory_init(int limit){
    //打印当前的虚拟内存限制
    struct rlimit old_limit;
    getrlimit(RLIMIT_AS,&old_limit);
    printf("the process's old soft virtual memory limit: 0x%lx MB\n",old_limit.rlim_cur/1024/1024);
    printf("the process's old hard virtual memory limit: 0x%lx MB\n",old_limit.rlim_max/1024/1024);
    printf("\n");
    // 设置当前进程的虚拟内存限制为limit
    struct rlimit new_limit;
    new_limit.rlim_cur = limit;
    new_limit.rlim_max = limit;
    setrlimit(RLIMIT_AS,&new_limit);
    getrlimit(RLIMIT_AS,&new_limit);
    printf("the process's new soft virtual memory limit: %ld KB\n",new_limit.rlim_cur/1024);
    printf("the process's new hard virtual memory limit: %ld KB\n",new_limit.rlim_max/1024);
    printf("\n");
}

//返回一个合适的块，或者返回NULL（在没有找到的情况下）
//last保存上一次遍历过的块，所以当没有找到合适的块的时候，malloc函数可以很轻松地去扩展堆的尾部（长度）
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

t_block extend_heap(t_block last, size_t size)
{
    t_block b;
    b = sbrk(0);//获取break指针地址
    if ((void *)-1 == sbrk(meta_data_size + size)) // meta_data_size+size=meta-data+数据块的内存大小
        return NULL;                               // sbrk失败,返回NULL
    b->size = size;
    b->next = NULL;
    b->prev = last;
    b->ptr = b->data;
    if (last) //不是第一个block
        last->next = b;//更新前驱的next指针
    b->free = 0;
    return b;
}

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

t_block get_block(void *p)
{ //获取数据块的首地址
    char *tmp;
    tmp = p;
    return (p = tmp -= meta_data_size);
}

int vaild_addr(void *p)
{
    if (base)
        if (p > base && p < sbrk(0))  //地址应该在之前malloc所分配的区域内，即在first_block和当前break指针范围内
            return p == (get_block(p)->ptr); //ptr指向data域，如果b->ptr == b->data，那么free的地址大概率valid
    return 0; //invalid address
}

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