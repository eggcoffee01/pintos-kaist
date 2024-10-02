/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

//파일 사이즈를 디스크 섹터 단위로 나눈거. 한 페이지에 섹터가 몇개 필요한지 계산한다.
struct bitmap *swap_table;
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE;


/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */

	//디스크를 가져온다.
	//첫번째 인자는 채널을 말하는데, 0번은 기본장치를 가리킴. 1번은 추가장치 채널을 말한다.
	//두번째 인자는 디스크의 종류(장치?)를 가리키는데, 0번은 주 디스크 1번은 보조 디스크. 즉 우리는 보조 디스크를 가져와야함.

	//즉 0,0은 운영체제나 파일 시스템이 위치하는 주 하드디스크.
	// 0, 1 은 추가 하드디스크, 보조 저장장치.
	// 1, 0 은 스크래치 디스크나 보조 디스크. >> 아마도 캐시? 파일이 빠르게 생성저장불러오기 되는 듯.
	// 1, 1은 스왑 디스크 등 성능을 위한 보조장치들.

	//스왑 디스크 설정
	swap_disk = disk_get(1, 1);

	//스왑 디스크의 여유 공간과 사용 영역을 확인하기 위한 무언가가 필요함. 스왑 영역은 PGSIZE 단위로 관리됨.
	//그렇다면? 남은 스왑 디스크의 자리를 계산할 수 있음. (int로 빈자리를 취급할 수 있음)

	//한 페이지에 필요한 섹터의 양을 계산한 다음, 할당받은 swap_disk의 사이즈를 계산함. 몇개의 페이지를 넣을 수 있는가?
	size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE;
	
	//테이블을 비트맵으로 관리하면 좋다. 어차피 할당됨/안됨 만 체크하기 때문.
	//이렇다면 파일 디스크립터도 비트맵으로 할 수 있는거 아님? 나중에 고려해 볼 것.
	swap_table = bitmap_create(swap_size);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {

	//물리 메모리가 없을 때 대비 = uninit 일때 anon으로 변경하기 위함.
	struct uninit_page *uninit = &page->uninit;
	memset(uninit, 0, sizeof(struct uninit_page));

	/* Set up the handler */
	page->operations = &anon_ops;

	//물리 메모리에 있음. 상태 표시 -1 말하자면 스왑에 없음.
	struct anon_page *anon_page = &page->anon;
	anon_page->swap_index = -1;

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	//고칠곳
	//스왑 디스크에서 익명 페이지를 옮김(도로 가져옴)
	// 스왑디스크 -> 메모리
	//데이터 위치는 페이지 구조체에 저장되어있음. (스왑 아웃할 떄 저장할 것.)
	//가져올 때 스왑 테이블을 업데이트 해야함.
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	//고칠곳
	//1순위
	//익명 페이지를 스왑 디스크로 스왑 아웃.
	//즉, 페이지 -> 스왑 디스크
	//1. 디스크에 빈 스왑 슬롯이 있는지 체크. 없다면 커널 패닉 일어남.
	//2. 메모리의 페이지 데이터를 디스크 슬롯에 복사.
	//3. 저장한 데이터 위치를 페이지 구조체에 저장.
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	//여기
	//익명 페이지가 보유하고 있는 리소스 해제.
	//구조체 자체를 해제할 필요 없음.
}
