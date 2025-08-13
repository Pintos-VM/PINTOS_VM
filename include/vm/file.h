#ifndef VM_FILE_H
#define VM_FILE_H
#include <string.h>

#include "filesys/file.h"
#include "threads/vaddr.h"

struct page;
enum vm_type;

struct file_page {
    struct file *file;    // 읽을 파일 핸들
    off_t offset;         // 이 페이지가 읽을 파일 오프셋
    uint32_t read_bytes;  // 이 페이지에서 파일로부터 읽을 바이트 수
    uint32_t zero_bytes;  // 이 페이지에서 0으로 채울 바이트 수
    bool writable;        // 페이지 쓰기 권한
};

struct file_aux {
    struct file *file;
    off_t offset;
    uint32_t read_bytes;
    uint32_t zero_bytes;
    bool writable;
};

void vm_file_init(void);
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset);
void do_munmap(void *va);
#endif
