/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "threads/malloc.h"
#include "vm/inspect.h"

static struct lock frame_lock;   // 전역 락
static struct list frame_table;  // 전역 프레임 테이블

static void frame_init(void) {
    lock_init(&frame_lock);
    list_init(&frame_table);
}

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
    void *va = pg_round_down(upage);

    /* Check wheter the upage is already occupied or not. */
    /* 이미 등록된 VA인지 확인 */
    if (spt_find_page(spt, va) != NULL)
        return false;

    /* page 할당 */
    struct page *page = calloc(1, sizeof(struct page));
    if (page == NULL) {
        free(aux);
        return false;
    }

    /* 페이지 초기화 */
    page->va = va;

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
    uninit_new(page, va, init, type, aux, page_initializer);

    page->owner = t;
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

static uint64_t spt_hash(const struct hash_elem *e, void *aux UNUSED) {
    const struct spt_entry *se = hash_entry(e, struct spt_entry, elem);
    return hash_bytes(&se->va, sizeof se->va);
}

static bool spt_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
    const struct spt_entry *sa = hash_entry(a, struct spt_entry, elem);
    const struct spt_entry *sb = hash_entry(b, struct spt_entry, elem);
    return sa->va < sb->va;
}

static void spt_destroy_elem(struct hash_elem *e, void *aux UNUSED) {
    struct spt_entry *se = hash_entry(e, struct spt_entry, elem);
    vm_dealloc_page(se->page);
    // // page/destroy/aux 정리는 정책에 맞게
    // // destroy(se->page);
    // // free(se->page);
    // free(se);
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *spt_find_page(struct supplemental_page_table *spt, void *addr) {
    /* TODO: Fill this function. */
    struct spt_entry key;
    key.va = pg_round_down(addr);
    struct hash_elem *e = hash_find(&spt->ht, &key.elem);
    if (!e) {
        return NULL;
    }

    struct spt_entry *se = hash_entry(e, struct spt_entry, elem);
    return se->page;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt, struct page *page) {
    /* TODO: Fill this function. */
    ASSERT(page != NULL);

    struct spt_entry *se = malloc(sizeof *se);
    if (!se) {
        return false;
    }

    se->va = pg_round_down(page->va);
    se->page = page;

    struct hash_elem *dup = hash_insert(&spt->ht, &se->elem);
    if (dup) {
        free(se);
        return false;
    }
    return true;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
    struct spt_entry key = {.va = page->va};
    struct hash_elem *e = hash_find(&spt->ht, &key.elem);
    if (e) {
        struct spt_entry *se = hash_entry(e, struct spt_entry, elem);
        hash_delete(&spt->ht, e);
        free(se);
    }
    // 페이지 해제는 호출자가 따로 destroy/free
    // vm_dealloc_page(page);
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
static void vm_stack_growth(void *addr UNUSED) {
    // TODO: 스택 성장 구현
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {
    return false;
}

static void vm_free_frame(struct frame *fr) {
    if (!fr) {
        return;
    }
    if (fr->kva) {
        palloc_free_page(fr->kva);
    }
    // frame_table에서 elem 제거 필요 시
    // list_remove(&fr->elem);
    free(fr);
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f, void *addr, bool user, bool write,
                         bool not_present) {
    // 1) 주소/컨텍스트 기본 필터
    if (addr == NULL)
        return false;

    // 유저 영역이 아닌 주소면 SPT로 처리하지 않음
    if (!is_user_vaddr(addr))
        return false;

    // 커널 컨텍스트에서 난 폴트면(= user == false) 여기서 끝
    if (!user)
        return false;

    struct thread *t = thread_current();
    struct supplemental_page_table *spt = &t->spt;
    void *va = pg_round_down(addr);  // 페이지 경계로 내리기

    /* 2) SPT에서 조회 */
    struct page *page = spt_find_page(spt, va);

    if (page) {
        /* 2-1) 페이지 메타는 있으나 물리 프레임 없음 -> not-present 폴트여야 정상 */
        if (not_present) {
            /* 실제로 들고 오기 (lazy load / zero-fill 등) */
            return vm_claim_page(va);
        } else {
            /* 2-2) 권한 위반 여부 판단: 쓰기 요청인데 R/O 매핑이면 실패 */
            if (write && !page->writable) {
                return false;
            }
            return false;
        }
    }

    /* 3) SPT 미스 -> 스택 성장 후보인지 판단 */
    if (user) {
        uintptr_t u_rsp = (uintptr_t)f->rsp;
        uintptr_t u_addr = (uintptr_t)addr;

        /* 스택은 하향 성장: addr <= rsp 이고, 소폭 여유 이내 접근 허용 */
        bool near_rsp = (u_addr <= u_rsp) && (u_rsp - u_addr <= 32);

        /* 최대 스택 한도/가드 정책 (1 << 20 = 1 MiB) */
        uintptr_t max_stack = USER_STACK - (1u << 20);
        bool within_limit = u_addr >= max_stack;

        if (near_rsp && within_limit) {
            /* 3-1) anon으로 lazy 등록 (aux 불필요) */
            if (!vm_alloc_page_with_initializer(VM_ANON, va, true, NULL, NULL)) {
                return false;
            }

            /* 3-2) 클레임 (프레임 확보 + 매핑) */
            return vm_claim_page(va);
        }
    }

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
    /* 2) 페이지 내용 채우기 */
    bool ok;
    ok = swap_in(page, frame->kva);
    // if (page->operations->type == VM_UNINIT) {
    //     /* 첫 접근: lazy initializer 실행
    //        - anon: zero-fill
    //        - file: file_read_at + zero-fill */
    //     ok = uninit_initialize(page, frame->kva);
    // } else {
    //     /* 이미 타입 확정된 페이지:
    //        - 스왑으로 나갔다가 돌아오는 경우 등 */
    //     ok = swap_in(page, frame->kva);
    // }

    if (!ok) {
        /* 롤백 */
        page->frame = NULL;
        frame->page = NULL;
        vm_free_frame(frame);
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
void supplemental_page_table_init(struct supplemental_page_table *spt) {
    hash_init(&spt->ht, spt_hash, spt_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {
    return false;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt) {
    /* TODO: Destroy all the supplemental_page_table hold by thread and
     * TODO: writeback all the modified contents to the storage. */
    hash_destroy(&spt->ht, spt_destroy_elem);
}
