// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009 Arnd Bergmann <arnd@arndb.de>
 * Copyright (C) 2012 Regents of the University of California
 */

#include <linux/linkage.h>
#include <linux/syscalls.h>
#include <asm-generic/syscalls.h>
#include <asm/syscall.h>

asmlinkage long __riscv_sys_revstr(char __user *str, size_t n);
asmlinkage long __riscv_sys_tempbuf(int mode, void __user *data, size_t size);

#undef __SYSCALL
#define __SYSCALL(nr, call)     [nr] = (call),

void * const sys_call_table[__NR_syscalls] = {
        [0 ... __NR_syscalls - 1] = sys_ni_syscall,
#include <asm/unistd.h>
};

