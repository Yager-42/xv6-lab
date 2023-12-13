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

struct run
{
  struct run *next;
};

struct
{
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU]; // 每个cpu一个

void kinit()
{
  for (int i = 0; i < NCPU; ++i)
  {
    char name[9] = {0};
    snprintf(name, 8, "kmem-%d", i);
    initlock(&kmem[i].lock, "kmem");
  }
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  push_off();//释放内存时不允许中断
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  int id = cpuid();

  r = (struct run *)pa;

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);

  pop_off();
}

void* ksteal(int cpu)
{
  struct run* r;
  for(int i=0;i<NCPU;++i)
  {
    int next_cpu = (cpu+i)%NCPU;

    acquire(&kmem[next_cpu].lock);
    r = kmem[next_cpu].freelist;
    //有我就偷一页
    if(r)
    {
      kmem[next_cpu].freelist = r->next;
    }
    release(&kmem[next_cpu].lock);
    if(r) break;//偷一页就走人
  }
  return (void*)r;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int id = cpuid();

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if (r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);

  //向全局页表拿，拿不到，去其他cpu偷一个
  if(r == 0)
  {
    r = ksteal(id);
  }

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  pop_off();
  return (void *)r;
}
