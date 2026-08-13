/* crt0_x64.S — Minimal Linux x86_64 C runtime startup
 * Assembled with: gcc -c crt0_x64.S -o crt0_x64.o
 *
 * Entry point: _start
 * Calls: main(argc, argv) 
 * On return: sys_exit(eax)
 * No libc dependency.
 */

.text
.global _start
.type _start, @function

_start:
    /* Linux puts argc at (%rsp), argv at 8(%rsp) */
    movq (%rsp), %rdi       /* argc → first arg */
    leaq 8(%rsp), %rsi      /* argv → second arg */
    
    /* Align stack to 16 bytes (SysV ABI requirement) */
    andq $-16, %rsp
    
    /* Call main(argc, argv) */
    call main
    
    /* sys_exit(return_value) */
    movl %eax, %edi         /* exit code */
    movl $60, %eax          /* __NR_exit */
    syscall

/* ================================================================
 * Minimal C library — direct Linux syscalls
 * ================================================================ */

.global puts
.type puts, @function
puts:
    /* puts(const char *s) — Watcom register convention: arg1 in EAX */
    movq %rax, %rsi         /* buf = string pointer from EAX */
    /* Find string length */
    xorq %rdx, %rdx         /* count = 0 */
.puts_len:
    cmpb $0, (%rsi,%rdx)
    je .puts_write
    incq %rdx
    jmp .puts_len
.puts_write:
    movq $1, %rdi           /* fd = stdout */
    movq $1, %rax           /* __NR_write */
    syscall
    /* Write newline */
    pushq $10               /* '\n' on stack */
    movq %rsp, %rsi
    movq $1, %rdx           /* count = 1 */
    movq $1, %rdi           /* fd = stdout */
    movq $1, %rax           /* __NR_write */
    syscall
    addq $8, %rsp
    xorl %eax, %eax         /* return 0 */
    ret

.global write
.type write, @function
write:
    /* ssize_t write(int fd, const void *buf, size_t count) */
    /* Args already in rdi, rsi, rdx per SysV ABI */
    movq $1, %rax           /* __NR_write */
    syscall
    ret

.global read
.type read, @function
read:
    movq $0, %rax           /* __NR_read */
    syscall
    ret

.global _exit
.type _exit, @function
_exit:
    movl %edi, %edi         /* status */
    movl $60, %eax          /* __NR_exit */
    syscall

/* Watcom runtime stubs — needed for -od compiled code */
.global __CHK
.type __CHK, @function
__CHK:
    /* Stack check — Watcom pushes stack size before calling.
     * Pop the argument and return. Linux kernel handles stack growth. */
    ret $8

.global _cstart
.type _cstart, @function  
_cstart:
    /* Watcom startup — redirect to _start */
    jmp _start

.section .note.GNU-stack,"",@progbits
