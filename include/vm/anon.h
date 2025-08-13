#ifndef VM_ANON_H
#define VM_ANON_H
struct page;
enum vm_type;

typedef size_t swap_slot;
#define SWAP_SLOT_NONE ((swap_slot)SIZE_MAX);

struct anon_page {
    bool zero_fill;
    size_t swap_slot;
    bool writable;
};

struct anon_aux {
    bool zero_fill;    // 첫 로드 zero-fill?
    size_t swap_slot;  // 스왑 슬롯(없으면 특수값)
    bool writable;     // 보통 true
};

void vm_anon_init(void);
bool anon_initializer(struct page *page, enum vm_type type, void *kva);

#endif
