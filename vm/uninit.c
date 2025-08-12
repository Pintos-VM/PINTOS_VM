/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * */

#include "vm/uninit.h"

#include "threads/malloc.h"
#include "vm/vm.h"

static bool uninit_initialize(struct page *page, void *kva);
static void uninit_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
    .swap_in = uninit_initialize,
    .swap_out = NULL,
    .destroy = uninit_destroy,
    .type = VM_UNINIT,
};

/* DO NOT MODIFY this function */
void uninit_new(struct page *page, void *va, vm_initializer *init, enum vm_type type, void *aux,
                bool (*initializer)(struct page *, enum vm_type, void *)) {
    ASSERT(page != NULL);

    *page = (struct page){.operations = &uninit_ops,
                          .va = va,
                          .frame = NULL, /* no frame for now */
                          .uninit = (struct uninit_page){
                              .init = init,
                              .type = type,
                              .aux = aux,
                              .page_initializer = initializer,
                          }};
}

/* Initalize the page on first fault */
static bool uninit_initialize(struct page *page, void *kva) {
    struct uninit_page *uninit = &page->uninit;

    vm_initializer *init = uninit->init;  // (struct page*, void*)
    void *aux = uninit->aux;
    enum vm_type type = uninit->type;  // ← 오타 수정

    // 1) 타입별 객체로 전환 (ops 교체 포함)
    if (!uninit->page_initializer(page, type, kva))
        return false;

    ASSERT(page->operations && page->operations->type != VM_UNINIT);

    // 2) (옵션) 2차 초기화기
    if (init && !init(page, aux))
        return false;

    // 3) 흔적 정리(선택)
    uninit->init = NULL;  // 사용 끝
    // aux는 타입별 initializer에서 free하는 정책이라면 여기선 건드리지 않음

    return true;
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
static void uninit_destroy(struct page *page) {
    struct uninit_page *uninit UNUSED = &page->uninit;
    /* TODO: Fill this function.
     * TODO: If you don't have anything to do, just return. */
    if (uninit->aux) {
        free(uninit->aux);
        uninit->aux = NULL;
    }
}
