#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include <stddef.h>
#include "riscv.h"
#include "vm.h"
#include "kalloc.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(uint64 val_va, int _tz)
{
    struct proc *p = curr_proc();
    uint64 pa = useraddr(p->pagetable, val_va);
    if (pa == 0)
        return -1;
    TimeVal *val = (TimeVal *)pa;
    uint64 cycle = get_cycle();
    val->sec  = cycle / CPU_FREQ;
    val->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
    return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
/*
* LAB1: you may need to define sys_task_info here
*/
uint64 sys_task_info(uint64 ti_va) {
    struct proc *p = curr_proc();
    if (ti_va == 0)
        return -1;
    uint64 pa = useraddr(p->pagetable, ti_va);
    if (pa == 0)
        return -1;
    struct TaskInfo *ti = (struct TaskInfo *)pa;
    ti->status = Running;
    uint64 elapsed_ms = 0;
    if (p->startTime != 0) {
        uint64 diff = get_cycle() - p->startTime;
        uint64 sec  = diff / CPU_FREQ;
        uint64 msec = (diff % CPU_FREQ) * 1000 / CPU_FREQ;
        elapsed_ms  = sec * 1000 + msec;
    }
    ti->time = (int)elapsed_ms;
    for (int i = 0; i < MAX_SYSCALL_NUM; i++) {
        ti->syscall_times[i] = p->syscall_times[i];
    }
    return 0;
}

extern char trap_page[];

uint64 sys_mmap(uint64 start, uint64 len, int port, int flag, int fd) {
    if (start % PGSIZE != 0)
    { 
        return -1;
    }
    if (len == 0)
    {
        return 0;
    }
    if (len > (1ULL << 30)) {
        return -1;
    }
    if (port & ~0x7){
        return -1;
    }
    if ((port & 0x7) == 0) {
        return -1;
    }
    struct proc *p = curr_proc();
    uint64 npages = (len + PGSIZE - 1) / PGSIZE;
    for (uint64 va = start; va < start + npages * PGSIZE; va += PGSIZE) {
        pte_t*pte = walk(p->pagetable, va, 0);
        if (pte != 0 && (*pte & PTE_V))
        return -1;
    }
    int perm = PTE_U;
    if (port & 0x1) perm |= PTE_R;
    if (port & 0x2) perm |= PTE_W;
    if (port & 0x4) perm |= PTE_X;

    for (uint64 i = 0; i < npages; i++) {
        void *pa = kalloc();
        if (pa == 0) {
            for (uint64 j = 0; j < i; j++)
                uvmunmap(p->pagetable, start + j * PGSIZE, 1, 1);
            return -1;
        }
        memset(pa, 0, PGSIZE);
        if (mappages(p->pagetable, start + i * PGSIZE, PGSIZE, (uint64)pa, perm) != 0) {
            kfree(pa);
            for (uint64 j = 0; j < i; j++)
                uvmunmap(p->pagetable, start + j * PGSIZE, 1, 1);
            return -1;
        }
    }

    uint64 end_page = (start + npages * PGSIZE) / PGSIZE;
    if (end_page > p->max_page)
        p->max_page = end_page;

    return 0;
}

uint64 sys_munmap(uint64 start, uint64 len)
{
    if (start % PGSIZE != 0)
        return -1;
    if (len == 0)
        return 0;

    struct proc *p = curr_proc();
    uint64 npages = (len + PGSIZE - 1) / PGSIZE;

    
    for (uint64 va = start; va < start + npages * PGSIZE; va += PGSIZE) {
        pte_t *pte = walk(p->pagetable, va, 0);
        if (pte == 0 || !(*pte & PTE_V))
            return -1;
    }

    uvmunmap(p->pagetable, start, npages, 1);
    return 0;
}

void syscall()
{
	struct proc *p = curr_proc();
    int id = p->trapframe->a7;
    int ret = 0;
    uint64 args[6] = { 
        p->trapframe->a0, p->trapframe->a1, p->trapframe->a2,
        p->trapframe->a3, p->trapframe->a4, p->trapframe->a5 
    };

    if (id >= 0 && id < MAX_SYSCALL_NUM) {
        p->syscall_times[id]++;
    }

    switch (id) {
        case SYS_write:
            ret = sys_write(args[0],args[1], args[2]);
            break;
        case SYS_exit:
            sys_exit(args[0]);
            break;
        case SYS_sched_yield:
            ret = sys_sched_yield();
            break;
        case SYS_gettimeofday:
        case SYS_clock_gettime:
        case 172:
            ret = sys_gettimeofday(args[0], args[1]);
            break;
        case SYS_task_info:
            ret = sys_task_info(args[0]);
            break;
        case 222:
            ret = sys_mmap(args[0], args[1], (int)args[2], (int)args[3], (int)args[4]);
            break;
        case 215: 
            ret = sys_munmap(args[0], args[1]);
            break;
        default:
            ret = -1;
            errorf("unknown syscall %d", id);
    }

    curr_proc()->trapframe->a0 = ret;
}