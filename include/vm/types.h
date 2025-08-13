#ifndef VM_TYPES_H
#define VM_TYPES_H

#include <stdbool.h>  // bool 사용
// #include <stdint.h> // 필요하면

/* 전방선언: 포인터/함수포인터 시그니처에만 사용되므로 크기 불필요 */
struct page;

/* vm_type은 여기서 실제 정의가 필요(전방선언 불가) */
enum vm_type {
    VM_UNINIT = 0,
    VM_ANON = 1,
    VM_FILE = 2,
    VM_PAGE_CACHE = 3,

    /* 상태/마커 비트 (확장용) */
    VM_MARKER_0 = (1 << 3),
    VM_MARKER_1 = (1 << 4),

    /* DO NOT EXCEED THIS VALUE */
    VM_MARKER_END = (1 << 31),
};

/* 2차 초기화기: lazy 시에 컨텐츠를 채우는 콜백
   -> 인자는 (page, aux) 두 개로 유지해야 uninit.c 등과 일관됩니다. */
typedef bool vm_initializer(struct page *, void *);

/* 하위 3비트로 타입 구분 */
#define VM_TYPE(type) ((type)&7)

#endif /* VM_TYPES_H */