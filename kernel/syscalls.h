#ifndef SYSCALLS_H
#define SYSCALLS_H

#define SYSCALL_READ      0
#define SYSCALL_WRITE     1
#define SYSCALL_OPEN      2
#define SYSCALL_CLOSE     3
#define SYSCALL_SEEK      8
#define SYSCALL_DUP       32
#define SYSCALL_NAONSLEEP 35
#define SYSCALL_EXIT      60
#define SYSCALL_MKDIR     83
#define SYSCALL_UNLINK    87

#define SYSCALL_MAX 255

#endif
