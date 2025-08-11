/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "threads/malloc.h"
#include "vm/inspect.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void) {
    vm_anon_init();
    vm_file_init();
#ifdef EFILESYS /* For project 4 */
    pagecache_init();
#endif
    register_inspect_intr();
    /* DO NOT MODIFY UPPER LINES. */
    /* TODO: Your code goes here. */
    frame_init();  // 프레임 락/테이블 초기화
}

static struct lock frame_lock;   // 전역 락
static struct list frame_table;  // 전역 프레임 테이블

static void frame_init(void) {
    lock_init(&frame_lock);
    list_init(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type page_get_type(struct page *page) {
    int ty = VM_TYPE(page->operations->type);
    switch (ty) {
        case VM_UNINIT:
            return VM_TYPE(page->uninit.type);
        default:
            return ty;
    }
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
                                    vm_initializer *init, void *aux) {
    ASSERT(is_user_vaddr(upage));  // 유저 영역 주소인지 확인
    ASSERT(VM_TYPE(type) != VM_UNINIT);
    ASSERT(pg_ofs(upage) == 0);  // 페이지 정렬 보장

    struct thread *t = thread_current();
    struct supplemental_page_table *spt = &t->spt;

    /* Check wheter the upage is already occupied or not. */
    /* 이미 등록된 VA인지 확인 */
    if (spt_find_page(spt, upage) != NULL)
        return false;

    /* page 할당 */
    struct page *page = malloc(sizeof(struct page));
    if (page == NULL) {
        free(aux);
        return false;
    }

    /* 페이지 소유자 등록 */
    page->owner = t;

    /* 타입별 page_initializer 선택 */
    bool (*page_initializer)(struct page *, enum vm_type, void *) = NULL;
    switch (VM_TYPE(type)) {
        case VM_ANON:
            page_initializer = anon_initializer;
            break;
        case VM_FILE:
            page_initializer = file_backed_initializer;
            break;
        default:
            free(aux);
            free(page);
            return false;
    }

    /* uninit 페이지로 생성 (init은 NULL로, 실제 로딩은 type별 init이 수행) */
    // init은 특별한 초기 동작(예: 특정 포맷 디코딩, 커스텀 읽기 전략 등)을
    // 페이지마다 다른 로딩 로직을 요구할 때 사용
    uninit_new(page, upage, NULL, type, aux, page_initializer);

    /* 쓰기 권한 보관 */
    page->writable = writable;

    /* SPT에 삽입 */
    if (!spt_insert_page(spt, page)) {
        /* 삽입 실패 시 자원 회수 */
        // uninit_new로 넘겨둔 aux도 여기서 회수해야 함
        free(aux);
        free(page);
        return false;
    }

    /* 여기서 프레임을 잡지 않는다! (lazy 유지) */
    return true;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
    struct page *page = NULL;
    /* TODO: Fill this function. */

    return page;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
    int succ = false;
    /* TODO: Fill this function. */

    return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
    vm_dealloc_page(page);
    return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim(void) {
    struct frame *victim = NULL;
    /* TODO: The policy for eviction is up to you. */

    return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *vm_evict_frame(void) {
    struct frame *victim UNUSED = vm_get_victim();
    /* TODO: swap out the victim and return the evicted frame. */

    return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *vm_get_frame(void) {
    lock_acquire(&frame_lock);

    struct frame *frame = malloc(sizeof(struct frame));
    if (!frame) {
        lock_release(&frame_lock);
        return NULL;
    }
    frame->page = NULL;
    frame->pinned = false;
    frame->kva = palloc_get_page(PAL_USER);

    if (frame->kva == NULL) {
        /* 1) 희생자 선정 (락 보유 상태) */
        // caller-holds-lock 규약
        struct frame *victim = vm_evict_frame();  // evict 함수 구현 필요
        ASSERT(victim && victim->page);
        victim->pinned = true;

        /* 2) I/O 언매핑은 락 없이 */
        lock_release(&frame_lock);

        /* 2) 스왑 아웃 (타입별 정책대로) */
        // 파일-백드: dirty면 정책에 따라 file_write_at 또는 다시 읽기 전용으로 버림
        // anon: 스왑 디바이스에 기록하고 page에 스왑 슬롯 인덱스 저장
        struct thread *owner = victim->page->owner;

        if (!swap_out(victim->page)) {
            // 스왑 공간 부족 등 비정상 상황 처리
            lock_acquire(&frame_lock);
            victim->pinned = false;
            lock_release(&frame_lock);
            free(frame);
            return NULL;
        }

        /* 3) TLB/PTE 정리 (희생자 소유자 주소공간에서 unmapping) */
        pml4_clear_page(owner->pml4, victim->page->va);
        victim->page->frame = NULL;  // 링크 끊기
        victim->page = NULL;

        /* 4) 다시 락 잡고 테이블 수정 + victim의 kva 재사용 */
        lock_acquire(&frame_lock);
        frame->kva = victim->kva;
        list_remove(&victim->elem);
        free(victim);
    }

    /* 새 프레임을 프레임 테이블에 등록 */
    list_push_back(&frame_table, &frame->elem);

    lock_release(&frame_lock);

    ASSERT(frame != NULL);
    ASSERT(frame->page == NULL);
    ASSERT(frame->kva != NULL);

    return frame;
}

/* Growing the stack. */
static void vm_stack_growth(void *addr UNUSED) {}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED,
                         bool write UNUSED, bool not_present UNUSED) {
    struct thread *t = thread_current();
    struct supplemental_page_table *spt = &t->spt;
    void *va = pg_round_down(addr);  // 페이지 경계로 내리기
    struct page *page = spt_find_page(spt, va);
    /* TODO: Validate the fault */
    /* TODO: Your code goes here */
    if (page == NULL) {
        /* 스택 성장 조건 검사 후 anon 페이지 할당 + claim
        return vm_stack_growth() */
        return false;
    }

    if (not_present && (page->frame == NULL)) {
        return vm_do_claim_page(page);
    }

    // 권한 위반 등은 false
    return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
    destroy(page);
    free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va) {
    struct thread *t = thread_current();
    struct supplemental_page_table *spt = &t->spt;

    /* TODO: Fill this function */
    /* 1) SPT에서 페이지 찾기 */
    struct page *page = spt_find_page(spt, va);
    if (page == NULL) {
        return false;
    }
    // (이미 claim된 페이지면 처리 정책에 따라 true/false 선택)
    if (page->frame) {
        return true;
    }

    /* 2) 실제 클레임 */
    return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool vm_do_claim_page(struct page *page) {
    struct thread *t = thread_current();

    /* 1) 프레임 할당*/
    struct frame *frame = vm_get_frame();
    if (frame == NULL) {
        return false;
    }
    /* Set links */
    frame->page = page;
    page->frame = frame;

    /* TODO: Insert page table entry to map page's VA to frame's PA. */
    /* 2) 페이지 내용 채우기 (lazy 로딩/zero-fill 등) */
    if (!swap_in(page, frame->kva)) {
        // 실패 시 링크 해제 + 프레임 반납
        page->frame = NULL;
        frame->page = NULL;
        vm_free_frame(frame);  // 함수 구현 필요 (테이블에서 제거 + palloc_free_page)
        return false;
    }

    /* 3) 페이지 테이블 매핑 (VA -> 프레임 KVA)*/
    if (!pml4_set_page(t->pml4, page->va, frame->kva, page->writable)) {
        // 매핑 실패 시 롤백
        destroy(page);
        page->frame = NULL;
        frame->page = NULL;
        vm_free_frame(frame);  // 함수 구현 필요 (테이블에서 제거 + palloc_free_page)
        return false;
    }
    return true;
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED) {
    /* TODO: Destroy all the supplemental_page_table hold by thread and
     * TODO: writeback all the modified contents to the storage. */
}
