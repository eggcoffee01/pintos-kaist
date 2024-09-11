#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "threads/synch.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

// struct lock filesys_lock;

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

/* File descriptor Macro */
#define FDCOUNT_LIMIT (1<<12)

void
syscall_init (void) {
	// lock_init(&filesys_lock);
	
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service routine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);


}

// 주소 값이 유저 영역에서 사용할 수 있는 주소 인지 확인하는 함수이다.
void check_address(void *addr){
	struct thread *t = thread_current();
	// 인자로 받아온 주소가 유저 영역의 주소가 아니거나, 해당 페이지가 존재하지 않을 때
	// 주소 자체가 NULL인 경우 프로그램을 종료한다. 
	
	if(!is_user_vaddr(addr) || addr == NULL){
		exit(-1);
	}
	if( pml4_get_page(t->pml4, addr) == NULL){
		exit(-1);
	}
	
}

// // 주어진 File descriptor를 이용해서, File descriptor table에서 파일 객체를 반환하는 함수이다.
// struct file *process_get_file(int fd){
// 	// 파일 디스크립터의 주소가 유효한 범위를 벗어나면 NULL 값을 반환한다.
// 	if(fd<0 || fd > FDCOUNT_LIMIT){
// 		return NULL;
// 	}
	
// 	struct thread *t = thread_current();
	
// 	// 현재 실행 중인 스레드의 File descriptor table을 가져온다.
// 	struct file **fdt = t->fdt;

// 	// File descriptor table 에서, 주어진 File descriptor에 해당하는 파일 객체를 가져온다.
// 	struct file *file = fdt[fd];

// 	// 현재 실행 중인 스레드의 File descriptor table에서 찾은 파일을 반환한다.
// 	return file;
// }


// #0. Pint OS를 종료하는 시스템 콜을 실행시킨다.
void halt(void){
	power_off();
}

// #1. 현재 실행 중인 프로세스만 종료하는 시스템 콜을 실행시킨다. 
void exit(int status){
	// 종료 시 프로세스 이름을 출력하고, 정상적으로 종료 시 status = 0 을 반환한다.
	struct thread *t = thread_current();
	printf( "%s: exit(%d)\n", t->name, status);
	thread_exit();
}


// #5. 파일을 생성하는 시스템 콜이다. 
// 파일 이름과 파일 크기를 인자 값으로 받아서, 파일을 생성하는 filesys_create() 함수를 쓴다.
// 성공적으로 파일 생성 시 True, 아닐 때는 False를 반환한다.
bool create(const char *file, unsigned init_size){
	// 주소 값이 유저 영역에서 사용하는 주소인지 확인한다.
	check_address(file);
	// 파일 이름과 크기를 인자 값으로 받아, 해당 인자에 맞는 파일을 생성한다.
	return filesys_create(file, init_size);

}


// #6. 해당되는 파일을 삭제하는 시스템 콜이다. 
// 성공적으로 파일을 삭제 시 True, 아닐 때는 False를 반환한다.
bool remove(const char *file){
	check_address(file);

	// 파일 이름에 해당되는 파일을 제거하는 함수이다.
	return filesys_remove(file);
}


// #10. 파일의 내용을 작성하는 시스템 콜이다.
int write(int fd, const void *buffer, unsigned size){
	// check_address(buffer);
	// struct file *file = process_get_file(fd);
	// int bytes_written = 0;

	// lock_acquire(&filesys_lock);

	// 표준 출력에 해당하는 File descriptor = 1인 경우, 버퍼에 있는 내용을 콘솔에 출력한다.
	if(fd == 1 && buffer != NULL){
		putbuf(buffer, size);
		return size;
	}

	// // 표준 입력에 해당하는 File descriptor = 0 인 경우, 출력과 상관 없기 때문에 -1을 반환한다.
	// else if(fd == 0){
	// 	return -1;
	// }

}


/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// 현재 실행되고 있던 thread의 rax 레지스터에 저장된 시스템 콜 번호를 이용해서,
	// 해당되는 시스템 콜 번호에 맞는 코드를 실행한다.

	int syscall_n = f->R.rax;

	switch(syscall_n)
	{
	case SYS_HALT:
		halt();
		break;

	case SYS_EXIT:
		exit(f->R.rdi);
		break;

	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	}

	// printf ("system call!\n");
	// thread_exit ();
}
