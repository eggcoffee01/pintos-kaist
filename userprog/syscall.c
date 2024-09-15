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
#include "filesys/file.h"
#include "threads/synch.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void check_address(void *addr);
struct file *process_get_file(int fd);
int process_add_file(struct file *file);
int write(int fd, const void *buffer, unsigned size);
bool create(const char *file, unsigned init_size);
bool remove(const char *file);
void seek (int fd, unsigned position);
unsigned tell(int fd);


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

/* Filesystem Lock */
struct lock filesys_lock;

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
	// TODO: Your implementation goes here.
	// 현재 실행되고 있던 thread의 rax 레지스터에 저장된 시스템 콜 번호를 이용해서,
	// 해당되는 시스템 콜 번호에 맞는 코드를 실행한다.

	int syscall_n = f->R.rax;

	switch(syscall_n)
	{
	
	// #0
	case SYS_HALT:
		halt();
		break;
	// #1
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	// #5
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	// #6
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	
	// #7
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	// #8
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;

	// #9
	case SYS_FILESIZE:
		filesize(f->R.rdi);
		break;

	// #10
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	// #11
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;

	// #12
	case SYS_TELL:
		tell(f->R.rdi);
		break;

	// #13
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	}

	// printf ("system call!\n");
	// thread_exit ();
}


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

// #5. 파일을 생성하는 시스템 콜이다. 
// 파일 이름과 파일 크기를 인자 값으로 받아서, 파일을 생성하는 filesys_create() 함수를 쓴다.
// 성공적으로 파일 생성 시 True, 아닐 때는 False를 반환한다.
bool create(const char *file, unsigned init_size){
	// 주소 값이 유저 영역에서 사용하는 주소인지 확인한다.
	check_address(file);
	
	// 파일 이름과 크기를 인자 값으로 받아, 해당 인자에 맞는 파일을 생성한다.
	bool result = filesys_create(file, init_size);
    
	return result;
	
}


// #6. 해당되는 파일을 삭제하는 시스템 콜이다. 
// 성공적으로 파일을 삭제 시 True, 아닐 때는 False를 반환한다.
bool remove(const char *file){
	check_address(file);

	// 파일 이름에 해당되는 파일을 제거하는 함수이다.
	return filesys_remove(file);
}


// #7. 주어진 File descriptor의 File에서 데이터를 읽는 시스템 호출이다.
int read(int fd, void *buffer, unsigned size){
	check_address(buffer);
	check_address(buffer+size-1);

	struct thread *curr = thread_current ();

	if(fd==0 && buffer!=NULL){
		/*
		input_getc();
		return size;
		*/
		for (int i = 0; i < size; i++) {
			((char *)buffer)[i] = input_getc();  // Store input into buffer
		}
		return size;  // Return the number of bytes read
	}
	else if(fd >= 2 && fd < FDCOUNT_LIMIT && buffer != NULL){
		//fd 테이블에서 페이지 포인터를 넘김
		if(curr->fdt[fd]){
			lock_acquire(&filesys_lock);
			int give_f_size = file_read(curr->fdt[fd], buffer, size);		
			lock_release(&filesys_lock);
			return give_f_size;	
		}
	}

	return -1;	
	

	/*
	check_address(buffer);
	check_address(buffer+size-1);
	
	// buffer를 unsigned char 포인터로 형변환하여 사용하기 위한 변수를 설정
	unsigned char *buff = buffer;
	int bytes_written;

	struct file *file = process_get_file(file);

	if (file == NULL){
		return -1;
	}
	// File descriptor 가 표준 입력인 경우, 키보드 입력을 받아서 buffer에 저장한다.
	if(fd ==0){
		char key;
		for (int bytes_written =0; bytes_written < size; bytes_written ++){
			key = input_getc();
			*buff++ = key;
			if (key =='\0'){
				break;
			}
		}
	}
	// File descriptor 가 표준 출력인 경우, -1을 반환한다.
	else if(fd == 1){
		return -1;
	}
	// 일반 파일일 경우, 파일을 읽어와서 buffer에 저장하고 읽은 바이트 수를 출력한다.
	else{
		lock_acquire(&filesys_lock);
		bytes_written = file_read(file, buffer, size);
		lock_release(&filesys_lock);
	}

	return bytes_written;
	*/


	/*
	struct thread *curr = thread_current();

	// 표준 입력인 경우 (File descriptor = 0인 경우), 키보드 입력을 받아서 buffer에 저장한다.
	if (fd == 0 && buffer != NULL){
		input_getc();
		
		return size;
	}
	// File descriptor가 2 이상일 경우, File을 읽어서 Buffer에 저장하고 읽은 바이트 수를 반환한다.
	else if(fd>=2 && fd < FDCOUNT_LIMIT && buffer!=NULL){
		if(curr->fdt[fd]){
			lock_acquire(&filesys_lock);
			int given_f_size = file_read(curr->fdt[fd], buffer, size);
			lock_release(&filesys_lock);
			return given_f_size;
		}
	}

	return -1;
	*/
}



// 현재 실행 중인 스레드의 File descriptor table에서 비어 있는 위치를 찾아서, 주어진 file 객체를 추가하는 함수이다. 
int process_add_file(struct file *file){
	struct thread *t = thread_current();
	struct file **fdt = t->fdt;
	int fd = t->fdidx;
	
	// File descriptor table에서 비어 있는 위치를 찾아서, 해당되는 file 객체를 추가한다.
	while (t->fdt[fd] != NULL && fd < FDCOUNT_LIMIT){
		fd ++;
	}

	if(fd >= FDCOUNT_LIMIT){
		return -1;
	}

	// File descriptor table에서 비어 있는 위치를 찾으면, 해당되는 위치에 file descriptor index 값을 갱신하고 file을 저장한다. 
	t->fdidx = fd;
	fdt[fd] = file;

	return fd;

}

// #9. 주어진 File descriptor에서 File 객체의 크기를 반환한다.
int filesize(int fd){
	// 주어진 File descriptor로부터 File 객체를 가져온다. 
	struct file *given_f = process_get_file(fd);

	// File이 존재하지 않을 경우 -1을 반환한다.
	if(given_f == NULL){
		return -1;
	}

	return file_length(given_f);
}


//#8. 해당되는 파일을 여는 시스템 콜이다.
int open(const char *file){
	check_address(file);

	struct thread *curr = thread_current ();

	if(file==NULL)
		return -1;

	struct file *f = filesys_open (file);
	
	if(f==NULL)
		return -1;
	
	for(int i=3; i < FDCOUNT_LIMIT; i++){
		if(curr->fdt[i] == NULL){
			if (!strcmp(thread_name(), file))
				file_deny_write(f);
			curr->fdt[i] = f;
			return i;
		}
	}
	file_close(f); 

	return -1;
	
	
	/*
	check_address(file);
	
	struct file *given_f = filesys_open(file);
	
	if(given_f == NULL){
		return -1;
	}

	// 각 프로세스(스레드)는 독립적인 File descriptor table을 갖고 있으므로, 
	// 파일을 열 때마다 해당 파일 객체를 File descriptor에 추가한다. 
	int fd = process_add_file(given_f);

	// File descriptor 추가 실패 시, 열었던 파일을 닫아준다.
	if(fd == -1){
		file_close(given_f);
		return -1;
	}

	return fd;
	*/
}



// 주어진 File descriptor를 이용해서, File descriptor table에서 File 객체를 반환하는 함수이다.
struct file *process_get_file(int fd){
	// File descriptor의 주소가 유효한 범위를 벗어나면 NULL 값을 반환한다.
	if(fd<0 || fd > FDCOUNT_LIMIT){
		return NULL;
	}
	
	struct thread *t = thread_current();
	
	// 현재 실행 중인 스레드의 File descriptor table을 가져온다.
	struct file **fdt = t->fdt;

	// File descriptor table 에서, 주어진 File descriptor에 해당하는 File 객체를 가져온다.
	struct file *file = fdt[fd];

	// 현재 실행 중인 스레드의 File descriptor table에서 찾은 File을 반환한다.
	return file;
}

// #10. 파일의 내용을 작성하는 시스템 콜이다.
int write(int fd, const void *buffer, unsigned size){
	check_address(buffer);
	struct file *file = process_get_file(fd);
	int bytes_written = 0;

	lock_acquire(&filesys_lock);

	// 표준 출력에 해당하는 File descriptor = 1인 경우, 버퍼에 있는 내용을 콘솔에 출력한다.
	if(fd == 1 && buffer != NULL){
		putbuf(buffer, size);
		bytes_written = size;
	}

	// 표준 입력에 해당하는 File descriptor = 0 인 경우, 출력과 상관 없기 때문에 -1을 반환한다.
	else if(fd == 0){
		// 표준 입력에 해당하는 File descriptor 일 경우, lock을 해제하고 -1을 반환한다.
		lock_release(&filesys_lock);
		return -1;
	}
	
	else if(fd>=2){
		// 파일의 내용을 작성하려는데 존재하지 않는다면, lock을 해제하고 -1을 반환한다.
		if(file == NULL){
			lock_release(&filesys_lock);
			return -1;
		}
		// 파일의 내용을 작성하려는데 존재한다면, buffer에 있는 내용을 file에 작성한다.
		bytes_written = file_write(file, buffer, size);
	}

	// 파일의 내용 작성을 마무리 했기 때문에 lock을 잠금 해제하고, 작성한 파일의 크기를 반환한다.
	lock_release(&filesys_lock);

	return bytes_written;
}


// #11. 주어진 File descriptor를 이용해서, 파일 내 지정된 위치로 이동하는 함수이다.
void seek (int fd, unsigned position){
	if(fd <2){
		return -1;
	}

	struct file *file = process_get_file(fd);

	check_address(file);

	if(file == NULL){
		return -1;
	}

	// 주어진 File descriptor를 이용해서, 파일 객체의 위치로 이동한다.
	file_seek(file, position);

}

// #12. 주어진 File descriptor 값을 이용해서, 파일의 위치를 확인하는 함수이다.
unsigned tell(int fd){

	struct file *file = process_get_file(file);
	check_address(file);

	if(file == NULL){
		return;
	}	

	if( fd >=2 && fd < FDCOUNT_LIMIT){
		// 파일 객체의 현재 위치를 반환한다.
		return file_tell(file);
	}
		
}

// #13. 주어진 File descriptor를 이용해서, 열린 파일을 닫는 함수이다.
void close(int fd){
	struct thread *curr = thread_current ();
	
	if(fd > 2 && fd < FDCOUNT_LIMIT){
		file_close(curr->fdt[fd]);
		curr->fdt[fd]=NULL;
	}

}
