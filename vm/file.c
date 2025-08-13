/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
    .swap_in = file_backed_swap_in,
    .swap_out = file_backed_swap_out,
    .destroy = file_backed_destroy,
    .type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void) {}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva) {
    /* Set up the handler */
    struct file_aux *aux = page->uninit.aux;
    struct file_page *fp = &page->file;

    page->operations = &file_ops;
    fp->file = aux->file;
    fp->offset = aux->offset;
    fp->read_bytes = aux->read_bytes;
    fp->zero_bytes = aux->zero_bytes;
    fp->writable = aux->writable;

    page->uninit.aux = NULL;
    free(aux);
    return true;
}

/* Swap in the page by read contents from the file. */
static bool file_backed_swap_in(struct page *page, void *kva) {
    struct file_page *fp = &page->file;
    off_t read_bytes = file_read_at(fp->file, kva, fp->read_bytes, fp->offset);
    if (read_bytes != (off_t)fp->read_bytes)
        return false;
    memset((uint8_t *)kva + fp->read_bytes, 0, PGSIZE - fp->read_bytes);
    return true;
}

/* Swap out the page by writeback contents to the file. */
static bool file_backed_swap_out(struct page *page) {
    struct file_page *fp = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void file_backed_destroy(struct page *page) {
    struct file_page *fp = &page->file;
    // file_close(fp->file);
}

/* Do the mmap */
void *do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset) {}

/* Do the munmap */
void do_munmap(void *addr) {}
