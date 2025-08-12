/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "threads/malloc.h"
#include "threads/mmu.h"
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
}
static unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED);
static bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
static void hash_elem_destructor(struct hash_elem* he, void* aux);

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
  page, do not create it directly and make it through this function or
  `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
                                    vm_initializer *init, void *aux) {
    ASSERT(VM_TYPE(type) != VM_UNINIT)

    struct supplemental_page_table *spt = &thread_current()->spt;

    /* Check wheter the upage is already occupied or not. */

    if (spt_find_page(spt, upage) == NULL) {
        /* TODO: Create the page, fetch the initialier according to the VM type,
         * TODO: and then create "uninit" page struct by calling uninit_new. You
         * TODO: should modify the field after calling the uninit_new. */
        struct page *page = malloc(sizeof(struct page));
        switch (type & VM_PAGE_CACHE) {
            case VM_ANON:
                uninit_new(page, upage, init, type, aux, anon_initializer);
                break;
            case VM_FILE:
                break;
            default:
                break;
        }

        page->writable = writable;

        if (!spt_insert_page(spt, page)) {
            return false;
        }

        return true;
    }

err:
    return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *spt_find_page(struct supplemental_page_table *spt, void *va) {
    // struct page *page = NULL;
    struct page p;
    p.va = pg_round_down(va);

    struct hash_elem *hash_elem = hash_find(&spt->spt_hash_table, &p.hash_elem);

    if (hash_elem) {
        return hash_entry(hash_elem, struct page, hash_elem);
    }

    return NULL;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt, struct page *page) {
    int succ = false;
    /* TODO: Fill this function. */
    if (hash_insert(&spt->spt_hash_table, &page->hash_elem) == NULL) {
        succ = true;
    }
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
  and return it. This always return valid address. That is, if the user pool
  memory is full, this function evicts the frame to get the available memory
  space.*/
static struct frame *vm_get_frame(void) {
    struct frame *frame = NULL;
    /* TODO: Fill this function. */
    frame = calloc(1, sizeof(struct frame));  // malloc + memset 0
    frame->page = NULL;
    if (frame == NULL)
        return NULL;
    if ((frame->kva = palloc_get_page(PAL_USER | PAL_ZERO)) == NULL)
        return NULL;
    // PANIC("todo");
    ASSERT(frame != NULL);
    ASSERT(frame->page == NULL);
    return frame;
}

//
/* Growing the stack. */
static void vm_stack_growth(void *addr) {
    vm_alloc_page_with_initializer(VM_ANON | VM_STACK, addr, true, NULL, NULL);
    if (!vm_claim_page(addr)) {
        msg("stack_grows error");
    }
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED,
                         bool write UNUSED, bool not_present UNUSED) {
    struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
    struct page *page = spt_find_page(spt, addr);

    /* TODO: Validate the fault */
    if (page != NULL)
        /* TODO: Your code goes here */
        return vm_do_claim_page(page);
    else {
        uint64_t prev_addr = pg_round_down(addr + 8);
        uint64_t page_addr = pg_round_down(addr);
        struct page *prev_page;
        if (prev_addr < USER_STACK && page_addr > (USER_STACK - (1 << 20)) &&
            (prev_page = spt_find_page(&thread_current()->spt, prev_addr)) != NULL) {
            if ((prev_page->anon.type & VM_STACK) == VM_STACK) {
                vm_stack_growth(page_addr);
                return true;
            }
        }
        return false;
    }
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
    destroy(page);
    free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va) {
    struct page *page = NULL;
    /* TODO: Fill this function */
    page = spt_find_page(&thread_current()->spt, va);

    return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool vm_do_claim_page(struct page *page) {
    struct frame *frame = vm_get_frame();

    /* Set links */
    frame->page = page;
    page->frame = frame;

    /* TODO: Insert page table entry to map page's VA to frame's PA. */
    if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable))
        return false;

    return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
    hash_init(&spt->spt_hash_table, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {
    struct hash_iterator i;
    hash_first(&i, &src->spt_hash_table);
    hash_next(&i);
    for (; i.elem != NULL; hash_next(&i)) {
        struct page *p = hash_entry(hash_cur(&i), struct page, hash_elem);
        struct page *new_page;
        switch (p->operations->type) {
            case VM_UNINIT:
                new_page = calloc(1,sizeof(struct page));

                struct lazy_read_file* lrf = calloc(1,sizeof(struct lazy_read_file));
                memcpy(lrf,p->uninit.aux, sizeof(struct lazy_read_file));
                uninit_new(new_page, p->va, p->uninit.init, p->uninit.type, lrf,
                           p->uninit.page_initializer); //안되면 new_page -> p 로 바꾸기
                hash_insert(&dst->spt_hash_table, &new_page->hash_elem);
                break;
            case VM_ANON:
                if (!vm_alloc_page(p->operations->type, p->va, p->writable)) {
                    free(new_page);
                    return false;
                }
                if (!vm_claim_page(p->va)) {
                    free(new_page);
                    return false;
                }
                new_page = spt_find_page(&dst->spt_hash_table, p->va);
                memcpy(new_page->frame->kva, p->frame->kva, PGSIZE);
                break;

            default:
                break;
        }
    }
    return true;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED) {
    /* TODO: Destroy all the supplemental_page_table hold by thread and
     * TODO: writeback all the modified contents to the storage. */
    // hash_clear(&spt->spt_hash_table, 구현한 hash_action_func);
    // 모든콘텐츠 저장소에 다시쓰기 미구현
    hash_clear(&spt->spt_hash_table, hash_elem_destructor);
}

/* Returns a hash value for page p. */
static unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED) {
    const struct page *p = hash_entry(p_, struct page, hash_elem);
    return hash_bytes(&p->va, sizeof p->va);
}

static bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
    const struct page *a = hash_entry(a_, struct page, hash_elem);
    const struct page *b = hash_entry(b_, struct page, hash_elem);

    return a->va < b->va;
}

static void hash_elem_destructor(struct hash_elem* he, void* aux){
    struct page* p = hash_entry(he,struct page, hash_elem);
    if(p->frame != NULL){
        free(p->frame);
    }
    free(p);
}
