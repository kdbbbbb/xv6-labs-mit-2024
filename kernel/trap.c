#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();


void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}
void
usertrap(void)
{
  int which_dev = 0;

  if ((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // 设置中断处理函数，切换到内核trap向量
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();

  // 保存用户态程序计数器
  p->trapframe->epc = r_sepc();

  if (r_scause() == 8) {
    // 用户态系统调用
    if (p->killed)
      exit(-1);

    // 跳过 ecall 指令
    p->trapframe->epc += 4;

    // 开启中断，处理后续中断
    intr_on();

    syscall();

  } else if ((which_dev = devintr()) != 0) {
    // 设备中断，正常处理

  } else if (r_scause() == 13 || r_scause() == 15|| r_scause() == 12) {
    // 页异常，可能是写时复制（COW）页面写异常
    uint64 va = r_stval();
    uint64 va_page = PGROUNDDOWN(va);

    if (va_page >= p->sz) {
      // 访问超出进程内存大小，杀死进程
      p->killed = 1;
      goto end;
    }

    pte_t *pte = cow_walk(p->pagetable, va_page);
    if (pte == 0) {
      // 不是有效的COW页，异常，杀死进程
      p->killed = 1;
      goto end;
    }

    uint64 pa = PTE2PA(*pte);
    int refcnt = get_mem_count(pa);

    if (refcnt < 1) {
      // 引用计数不应为0或负，异常处理
      p->killed = 1;
      goto end;
    } else if (refcnt > 1) {
      // 共享物理页，需申请新页并复制内容

      char *mem = kalloc();
      if (mem == 0) {
        // 分配失败，杀死进程
        p->killed = 1;
        goto end;
      }

      // 拷贝旧页面内容到新页
      memmove(mem, (char*)pa, PGSIZE);

      // 解除旧页映射，不释放物理页
      uvmunmap(p->pagetable, va_page, 1, 0);

      // 映射新页，设置用户读写执行权限
      if (mappages(p->pagetable, va_page, PGSIZE, (uint64)mem,
                   PTE_U | PTE_R | PTE_W | PTE_X) < 0) {
        kfree(mem);
        p->killed = 1;
        goto end;
      }

      // 减少旧物理页引用计数
      mem_count_down(pa);

    } else if (refcnt == 1) {
      // 只有一个引用，恢复页写权限，清除COW标志

      *pte |= PTE_W;        // 允许写
      *pte &= ~PTE_RSW;     // 清除COW标志
      sfence_vma();         // 关键：刷新TLB，确保新PTE生效
      
      // 这里可能需要刷新 TLB，调用 sfence_vma()（如果你的 xv6 有此函数）
    } else {
      // 其他情况异常
      p->killed = 1;
      goto end;
    }

  } else {
    // 未知异常，打印信息并杀死进程
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    p->killed = 1;
  }

end:
  if (p->killed)
    exit(-1);

  // 时钟中断让出CPU
  if (which_dev == 2)
    yield();

  usertrapret();
}



//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // interrupt or trap from an unknown source
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  w_stimecmp(r_time() + 1000000);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if(scause == 0x8000000000000009L){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000005L){
    // timer interrupt.
    clockintr();
    return 2;
  } else {
    return 0;
  }
}

