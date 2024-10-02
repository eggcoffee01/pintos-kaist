/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"
#include "string.h"
#include "userprog/process.h"

struct lock spt_lock;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	lock_init(&spt_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		struct page *p = malloc(sizeof (struct page));
		if(p==NULL){
			goto err;	
		}
		
		bool (*initializer)(struct page *, enum vm_type, void *) = NULL;
		
		switch (VM_TYPE(type))
		{
		case VM_ANON:
			initializer = anon_initializer;
			break;
		case VM_FILE:
			initializer = file_backed_initializer;
			break;
		default:
			break;
		}
		
		uninit_new(p, upage, init, VM_TYPE(type), aux, initializer);
		
		p->writable = writable;
		
		/* TODO: Insert the page into the spt. */
		spt_insert_page(spt, p);
	}
	return true;
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	/* TODO: Fill this function. */
	return page_lookup(spt, pg_round_down(va));
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */

	if(hash_insert(&spt->sp_table, &page->hash_elem) == NULL) 
		succ = true;


	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {

	hash_delete(&spt->sp_table, &page->hash_elem);
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	void *kva = palloc_get_page(PAL_USER);
	
	if(kva == NULL) PANIC("todo");

	frame = malloc(sizeof (struct frame));
	frame->kva = kva;
	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	vm_alloc_page(VM_ANON | VM_MARKER_0, pg_round_down(addr), 1);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	if (addr == NULL)
        return false;

    if (is_kernel_vaddr(addr))
        return false;

	
	//만약에 lazy 로 인해 발생한 인터럽트라면 
	if(not_present){

		//uintptr_t rsp = f->rsp; // user access인 경우 rsp는 유저 stack을 가리킨다.
		void *rsp = f->rsp; // user access인 경우 rsp는 유저 stack을 가리킨다.
        if (!user)            // kernel access인 경우 thread에서 rsp를 가져와야 한다.
            rsp = thread_current()->rsp;

		//스택 확장 (8바이트 차이. push함.)
		if (USER_STACK - (1 << 20) <= rsp - 8 && rsp - 8 == addr && addr <= USER_STACK){
            vm_stack_growth(addr);
		}

		//스택 확장. (메모리 공간 부족, 추가 요청.)
        else if (USER_STACK - (1 << 20) <= rsp && rsp <= addr && addr <= USER_STACK){
			vm_stack_growth(addr);
		}

		page = spt_find_page(spt, pg_round_down(addr));
		if(page == NULL) 
			return false;
		if (write == 1 && page->writable == 0) // write 불가능한 페이지에 write 요청한 경우
            return false;
		
		return vm_do_claim_page (page);
	}

	//다 아님
	return false;	
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt, va);

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;
	
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable);

	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->sp_table, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {

	//모든 hash의 요소를 순회하며 해당 함수 실행. 
	hash_apply(&src->sp_table, spt_copy);
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

	//hash 테이블에서 모든 요소를 제거하는 함수.
	//추가 함수를 통해 메모리 할당 해제를 할 수 있다. 
	hash_clear(&spt->sp_table, spt_kill);
	//free(&spt->sp_table.buckets);

}

unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED){
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
}

bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED){
	const struct page *a = hash_entry(a_, struct page, hash_elem);
	const struct page *b = hash_entry(b_, struct page, hash_elem);

	return a->va < b->va;
}

struct page *page_lookup(struct supplemental_page_table *spt, const void *va){
	struct page p;
	struct hash_elem *e;
	
	p.va = va;
	e = hash_find(&spt->sp_table, &p.hash_elem);
	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}



// action (list_elem_to_hash_elem (elem), h->aux); 의 형태로 실행됨.
//hash의 elem과 보조함수 aux가 주어짐. elem만 써도 페이지를 구할 수 있음.
void spt_copy(struct hash_elem *e,void *aux UNUSED){
	//페이지로 변환해옴.
	struct page *p = hash_entry(e, struct page, hash_elem);
	
	//해당 페이지 타입에 따라 구별
	enum vm_type type = VM_TYPE(p->operations->type);
	switch (type)
	{
		case VM_UNINIT:
			uninit_copy(p);
			break;
		case VM_ANON:
			anon_copy(p);
			break;
		case VM_FILE:
			file_copy(p);
			break;

		default:
			break;
	}
}

//초기화 안된 페이지 복사
void uninit_copy(struct page *p){
	enum vm_type type = p->uninit.type;
	void *upage = p->va;
	bool writable = p->writable;

	//초기화를 위한 함수와 추가 데이터 복제.
	vm_initializer *init = p->uninit.init;
	
	//다른 공간이어야하므로, malloc으로 새 공간 할당받음.
	void *aux = malloc(sizeof(struct load_aux));
	memcpy(aux, p->uninit.aux, sizeof(struct load_aux));

	//새 페이지 할당.
	vm_alloc_page_with_initializer(type, upage, writable, init, aux);
}

void anon_copy(struct page *p){
	void *upage = p->va;
	bool writable = p->writable;
	
	//새 페이지 할당.
	vm_alloc_page_with_initializer(VM_ANON, upage, writable, NULL, NULL);

	//초기화가 된 페이지이므로 데이터를 사용할 수 있어야함.
	//claim_page를 통해 물리 메모리 할당.
	struct page *newpage = spt_find_page(&thread_current()->spt, upage);
	vm_do_claim_page(newpage);

	//부모 페이지의 내용을 자식 페이지로 복사.
	memcpy(newpage->frame->kva, p->frame->kva, PGSIZE);
}

void file_copy(struct page *p){
	//나중에 구현할 것.
}

void spt_kill(struct hash_elem *e,void *aux UNUSED){
	//페이지와 타입을 구함
	struct page *p = hash_entry(e, struct page, hash_elem);
	// enum vm_type type = VM_TYPE(p->operations->type);

	// if(type == VM_FILE){
	// 	munmap(p->va);
	// }


	destroy(p);	
	free(p);
}