/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "threads/mmu.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
	
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}



/* Do the mmap */
static bool
lazy_load_mmap (struct page *page, void *aux) {
	struct load_aux *l = aux;

	file_seek(l->file, l->ofs);

	if(file_read(l->file, page->frame->kva, l->page_read_bytes) == NULL){
		palloc_free_page(page->frame->kva);
		return false;
	}

	memset(page->frame->kva + l->page_read_bytes, 0, l->page_zero_bytes);
	pml4_set_dirty(thread_current()->pml4, page->va, false);
	return true;
}

void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {

	// 주소 addr에 저장.
	// offset 바이트에서부터 length 만큼을 읽어올것.
	// 성공시 주소, 실패시 NULL

	// 1. 필요 페이지 크기 계산.
	// 파일의 길이는 배수가 아닐 때 매핑된 페이지가 잘린다 했으므로, up을 사용.

	// //기존 파일을 독립적으로 재참조 할 수 있게끔, 다시 열어줌.
	// //페이지 만큼 순회.
	// 	//인자 전달용 aux를 선언.

	uint64_t ori_addr = (uint64_t)addr;
    size_t read_bytes = length > file_length(file) ? file_length(file) : length;
    size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	struct file * open_file = file_reopen(file);

	while (read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

        struct load_aux *aux = (struct load_aux*)malloc(sizeof(struct load_aux));
        aux->file = open_file;
        aux->ofs = offset;
        aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;

		if (!vm_alloc_page_with_initializer (VM_FILE, addr, writable, lazy_load_mmap, aux)) {
			return NULL;
        }
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr       += PGSIZE;
		offset     += page_read_bytes;
	}

	//pml4_set_dirty(thread_current()->pml4, page->va, 0);

	return (void*)ori_addr;
}

/* Do the munmap */
void do_munmap (void *addr) {
    while(true){
        struct page* page = spt_find_page(&thread_current()->spt, addr);

        if(page == NULL)
            break;

        struct load_aux *aux = (struct load_aux *)page->uninit.aux;

        // dirty check
        if(pml4_is_dirty(thread_current()->pml4, page->va)){
            file_write_at(aux->file, addr, aux->page_read_bytes, aux->ofs);
            pml4_set_dirty(thread_current()->pml4, page->va, 0);
        }

        pml4_clear_page(thread_current()->pml4, page->va);
        addr += PGSIZE;
		
    }
}