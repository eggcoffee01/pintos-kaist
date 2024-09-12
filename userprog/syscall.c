#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/init.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "threads/synch.h"
#include "string.h"
#include "userprog/process.h"
#include "threads/palloc.h"


typedef int pid_t;
struct lock filesys_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void halt (void);
void exit (int status);
pid_t fork (const char *thread_name, struct intr_frame *f);
int exec (const char *cmd_line);
int wait (pid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
int process_add_file(struct file *f);
struct file *process_get_file(int fd);
void process_close_file(int fd);
void check_ptr(const uint64_t *ptr);


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

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	
	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	
	uint64_t number = f->R.rax;
	switch (number)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f);
		break;
	case SYS_EXEC:
		f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		lock_release(&filesys_lock);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		lock_release(&filesys_lock);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		lock_release(&filesys_lock);

		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		lock_release(&filesys_lock);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		lock_release(&filesys_lock);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	default:
		break;
	}
}

void halt (void){
	power_off();
}
void exit (int status){
	thread_current()->exit_status = status;
	thread_current()->user_exit = true;
	thread_exit();
}

pid_t fork (const char *thread_name, struct intr_frame *f){
	return process_fork(thread_name, f);
}

int exec(const char *cmd_line){
	check_ptr(cmd_line);
	if(thread_current()->exec_file != NULL) {
		file_close(thread_current()->exec_file);
		thread_current()->exec_file = NULL;
	}

	char *fn_copy = palloc_get_page(0);
	if(fn_copy == NULL) return -1;
	strlcpy(fn_copy, cmd_line, PGSIZE);
	
	if (process_exec(fn_copy) == -1) {
		exit(-1);
	}	
	return -1;
}

int wait (pid_t pid){
	return process_wait(pid);
}

bool create (const char *file, unsigned initial_size){
	check_ptr(file);
	
	lock_acquire(&filesys_lock);
	
	if(file[0] == '\0'|| file == NULL || strlen(file) > 16 || initial_size < 0)return 0;
	return filesys_create(file, initial_size);
}

bool remove (const char *file){	
	check_ptr(file);
	
	lock_acquire(&filesys_lock);

	return filesys_remove(file);
}

int open (const char *file){
	check_ptr(file);
	
	lock_acquire(&filesys_lock);
	if(file[0] == '\0' || file == NULL)return -1;
	
	struct file *f = filesys_open(file);

	if(f == NULL) return -1;
	int fd = process_add_file(f);
	if(fd == -1)
		file_close(f);
	return fd;
}

int filesize (int fd){
	return file_length(thread_current()->fdt[fd]);
}

int read (int fd, void *buffer, unsigned size){
	
	check_ptr(buffer);
	lock_acquire(&filesys_lock);


	if(!(0 <= fd && fd < maxfd) || (fd != 0 && process_get_file(fd) == NULL)) return 0;
	
	struct file* f = process_get_file(fd);
	if(fd == 0){
		for(int i = 0; i < size; i++){
			((char *)buffer)[i] = input_getc();
		}	
		return size;
	}
	else{	
		return file_read(f, buffer, size);
	}
}

int write (int fd, const void *buffer, unsigned size){
	check_ptr(buffer);

	lock_acquire(&filesys_lock);
	
	
	
	if(!(0 <= fd && fd < maxfd) || (fd != 1 && process_get_file(fd) == NULL)) return 0;
	


	if(fd == 1){
		putbuf(buffer, size);
		return size;
	}
	else{	
		struct file* f = process_get_file(fd);
		return file_write(f, buffer, size);
	}
}

void seek (int fd, unsigned position){
	struct file *f = process_get_file(fd);
	file_seek(f, position);
}

unsigned tell (int fd){
	struct file *f = process_get_file(fd);
	return file_tell(f);
}

void close (int fd){
	if(2 < fd && fd <= maxfd)
		process_close_file(fd);
	else
		exit(-1);
}

int process_add_file(struct file *f){
	int fd = -1;
	for(int i = 3; i < maxfd; i++){
		if(thread_current()->fdt[i] == NULL){
			fd = i;
			thread_current()->fdt[fd] = f;
			break;
		}
	}
	return fd;
}

struct file *process_get_file(int fd){
	return thread_current()->fdt[fd];
}

void process_close_file(int fd){
	struct file *f = process_get_file(fd);
	if(f != NULL)
		file_close(f);
	thread_current()->fdt[fd] = NULL;
}

void check_ptr(const uint64_t *ptr){
	if(ptr == NULL ||  !is_user_vaddr(ptr) || pml4_get_page(thread_current()->pml4, ptr) == NULL)
		exit(-1);
}