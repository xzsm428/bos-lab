#ifndef __MEMORY_H__
#define __MEMORY_H__
#include<stddef.h>

#define align4(x) (((((x)-1) >> 2) << 2) + 4) //用于将指针和整型大小对齐，meta-data已对齐，所以只需对齐数据块大小
#define meta_data_size 20                     //size of meta-data,不包括尾部用于分割的char data[1]

typedef struct s_block *t_block; //meta-data
struct s_block
{
    size_t size;
    t_block next; //后继
    t_block prev; //前驱
    int free;     //1空闲 0不空闲
    void *ptr;    //指向数据块的第一个字节
    char data[1]; //指向meta-data尾部，用于分割meta-data和数据块
};

void memory_init(int limit);
void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *p, size_t size);

#endif