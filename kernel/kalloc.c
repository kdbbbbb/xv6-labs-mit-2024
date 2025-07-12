// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"



void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct
 {
 struct spinlock lock;// 若有多个进行同时对数组进行操作，需要上锁
 int mem_count[PHYSTOP/PGSIZE];
 }mem_ref_struct;


void
kinit()
{
 initlock(&kmem.lock, "kmem");
 // 初始化mem_ref_struct的锁
 initlock(&mem_ref_struct.lock, "mem_ref");
 freerange(end, (void*)PHYSTOP);
} 


int get_mem_count(uint64 pa){
 int count;
 acquire(&mem_ref_struct.lock);
 count = mem_ref_struct.mem_count[(uint64)pa / PGSIZE];
 release(&mem_ref_struct.lock);
 return count;
 }

void mem_count_up(uint64 pa){
 acquire(&mem_ref_struct.lock);
 ++ mem_ref_struct.mem_count[(uint64)pa / PGSIZE];
 release(&mem_ref_struct.lock);
 }

int mem_count_down(uint64 pa){
 int flag = 0;
 acquire(&mem_ref_struct.lock);
 if((-- mem_ref_struct.mem_count[(uint64)pa / PGSIZE]) == 0){
 flag = 1;
 }
 release(&mem_ref_struct.lock);
 return flag;
 }


void mem_count_set_one(uint64 pa){
 acquire(&mem_ref_struct.lock);
 mem_ref_struct.mem_count[(uint64)pa / PGSIZE] = 1;
 release(&mem_ref_struct.lock);
 }
 
 void
freerange(void *pa_start, void *pa_end)
{
 char *p;
 p = (char*)PGROUNDUP((uint64)pa_start);
 for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
 // 系统初始化时会将内存引用减1，所以这里先设为1
 mem_count_set_one((uint64)p);
 kfree(p);
 }
}

void
kfree(void *pa)
{
  struct run *r;

  // 检查传入地址是否对齐，是否在合法物理内存范围内
  if (((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  r = (struct run*)pa;

  // 假设有一个函数 mem_count_down 返回引用计数递减后的值
  // 这里判断引用计数是否为1，说明此时需要释放物理页
  if (mem_count_down((uint64)pa) == 1) {
    // 释放前填充数据，防止悬挂指针
    memset(pa, 1, PGSIZE);

    // 释放物理内存，将该页加入空闲链表
    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    mem_count_set_one((uint64)r); // 引用计数设为 1
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // 清理 junk

  return (void*)r;
}
