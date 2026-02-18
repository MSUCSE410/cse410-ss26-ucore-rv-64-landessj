#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include <stddef.h>
#include "riscv.h"

uint64 sys_write(int fd, char *str, uint len)
{
	debugf("sys_write fd = %d str = %x, len = %d", fd, str, len);
	if (fd != STDOUT)
		return -1;
	for (int i = 0; i < len; ++i) {
		console_putchar(str[i]);
	}
	return len;
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

uint64 sys_gettimeofday(TimeVal *val, int _tz)
{
	if (val == NULL) return -1;
    
    uint64 cycle = get_cycle();
    val->sec = cycle / CPU_FREQ; 
    val->usec = ((cycle % CPU_FREQ) * 1000000) / CPU_FREQ;
    return 0;
}

/*
* LAB1: you may need to define sys_task_info here
*/
uint64 sys_task_info(uint64 ti_ptr) {
    struct proc *p = curr_proc();
    if (ti_ptr == 0) return -1;

    struct TaskInfo *user_ti = (struct TaskInfo *)ti_ptr;

    user_ti->status = Running;

    uint64 elapsed_ms = 0;
    if (p->startTime != 0) {
        uint64 diff = get_cycle() - p->startTime;
        uint64 sec = diff / CPU_FREQ;
        uint64 msec = (diff % CPU_FREQ) * 1000 / CPU_FREQ;
        elapsed_ms = sec * 1000 + msec;
    }
    user_ti->time = (int)elapsed_ms;

    for (int i = 0; i < MAX_SYSCALL_NUM; i++) {
        user_ti->syscall_times[i] = p->syscall_times[i];
    }

    return 0;
}

extern char trap_page[];

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
            ret = sys_write(args[0], (char *)args[1], args[2]);
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
            ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
            break;
        case SYS_task_info:
            ret = sys_task_info(args[0]);
            break;
        default:
            ret = -1;
            errorf("unknown syscall %d", id);
    }

    curr_proc()->trapframe->a0 = ret;
}
