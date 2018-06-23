// Copyright 2014 Wouter van Oortmerssen. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
Very fast, low overhead slab allocator.

In the common case, each size of allocation has its own bucket (a linked list of blocks of exactly
that size), meaning allocation is almost as fast as de-linking an element.

It acquires new blocks by allocating a page from the system, and subdividing that page into blocks
of the same size, which it then adds to the bucket all at once.

Each page has a page header that keeps track of how much of the page is in use. The allocator can
access the page header from any memory block because pages are allocated aligned to their sizes
(by clearing the lower bits of any pointer therein). To do this alignment, many pages at once are
allocated from the system, which wastes 1 page on alignment, currently representing 1% of memory.

Because each page tracks the number of blocks in use, the moment any page becomes empty, it will
remove all blocks therein from its bucket, and then make the page available to a different size
allocation. This avoids that if at some point a lot of blocks of a certain size were allocated that
they will always be allocated.

There aren't a lot of realistic worst cases for this allocator. To achieve a hypothetical worst
case, you'd have to:
- allocate lots of objects of size N
- deallocate most of them, but not all
- the ones that you don't deallocate would have to each exactly keep 1 page "alive"
  (i.e. if N = 100, there will be 20 blocks to a page, meaning if you didn't deallocate every 20th
  object allocated, that would create maximum waste.)
- never use objects of size N again after this.

This allocator can offer major cache advantages, because objects of the same type are often
allocated closer to eachother.

Alloc/dealloc do NOT store the size of the object, for a savings of size_t per object.
This assumes you know the size of the object when deallocating.
alloc_sized/dealloc_sized instead do store the size, if a more drop-in replacement for malloc/free
is desired.
*/

#ifndef NDEBUG
    // Uncomment if debugging with crtdbg functionality is required (for finding memory corruption).
    //#define PASSTHRUALLOC
    #define COLLECT_STATS
#endif

class SlabAlloc {
    // Must be ^2. lower means more blocks have to go thru the traditional allocator (slower).
    // Higher means you may get pages with only few allocs of that unique size (memory wasted).
    // On 32bit, 32 means all allocations <= 256 bytes go into buckets (in increments of 8 bytes
    // each).
    enum { MAXBUCKETS = 32 };
    // Depends on how much you want to take from the OS at once: PAGEATONCE*PAGESIZEF
    // You will waste 1 page to alignment with MAXBUCKETS at 32 on a 32bit system, PAGESIZEF is
    // 2048, so this is 202k.
    enum { PAGESATONCE = 101 };

    // "64bit should be enough for everyone". Everything is twice as big on 64bit: alignment,
    // memory blocks, and pages.
    enum { PTRBITS = sizeof(char *)==4 ? 2 : 3 };
    // Must fit 2 pointers in smallest block for doubly linked list.
    enum { ALIGNBITS = PTRBITS+1 };
    enum { ALIGN = 1<<ALIGNBITS };
    enum { ALIGNMASK = ALIGN-1 };
    enum { MAXREUSESIZE = (MAXBUCKETS-1)*ALIGN };
    // The largest block will fit almost 8 times.
    enum { PAGESIZEF = MAXBUCKETS*ALIGN*8 };
    enum { PAGEMASK = (~(PAGESIZEF-1)) };
    enum { PAGEBLOCKSIZE = PAGESIZEF*PAGESATONCE };

    struct PageHeader : DLNodeRaw {
        int refc;
        int size;
        char *isfree;
    };

    inline int bucket(int s) {
        return (s+ALIGNMASK)>>ALIGNBITS;
    }

    inline PageHeader *ppage(const void *p) {
        return (PageHeader *)(((size_t)p)&PAGEMASK);
    }

    inline int numobjs(int size) { return (PAGESIZEF-sizeof(PageHeader))/size; }

    DLList<DLNodeRaw> reuse[MAXBUCKETS];
    DLList<PageHeader> freepages, usedpages;
    void **blocks;

    DLList<DLNodeRaw> largeallocs;

    #ifdef COLLECT_STATS
    long long stats[MAXBUCKETS];
    #endif
    long long statbig;

    void putinbuckets(char *start, char *end, int b, int size) {
        assert((int)sizeof(DLNodeRaw) <= size);
        for (end -= size; start<=end; start += size) {
            reuse[b].InsertAfterThis((DLNodeRaw *)start);
        }
    }

    void newpageblocks() {
        // If we could get page aligned memory here, that would be even better.
        void **b = (void **)malloc(PAGEBLOCKSIZE+sizeof(void *));
        assert(b);
        *b = (void *)blocks;
        blocks = b;
        b++;
        char *first = ((char *)ppage(b))+PAGESIZEF;
        for (int i = 0; i<PAGESATONCE-1; i++) {
            PageHeader *p = (PageHeader *)(first+i*PAGESIZEF);
            freepages.InsertAfterThis(p);
        }
    }

    void *newpage(int b) {
        assert(b);
        if (freepages.Empty()) newpageblocks();
        PageHeader *page = freepages.Get();
        usedpages.InsertAfterThis(page);
        page->refc = 0;
        page->size = b*ALIGN;
        putinbuckets((char *)(page+1), ((char *)page)+PAGESIZEF, b, page->size);
        return alloc_small(page->size);
    }

    void freepage(PageHeader *page, int size) {
        for (char *b = (char *)(page+1); b+size<=((char *)page)+PAGESIZEF; b += size)
            ((DLNodeRaw *)b)->Remove();
        page->Remove();
        freepages.InsertAfterThis(page);
    }

    void *alloc_large(size_t size) {
        statbig++;
        DLNodeRaw *buf = (DLNodeRaw *)malloc(size + sizeof(DLNodeRaw));
        largeallocs.InsertAfterThis(buf);
        return ++buf;
    }

    void dealloc_large(void *p) {
        DLNodeRaw *buf = (DLNodeRaw *)p;
        --buf;
        buf->Remove();
        free(buf);
    }

    public:
    SlabAlloc() : blocks(nullptr), statbig(0) {
        for (int i = 0; i<MAXBUCKETS; i++) {
            #ifdef COLLECT_STATS
                stats[i] = 0;
            #endif
        }
    }

    ~SlabAlloc() {
        while(blocks) {
            void *next = *blocks;
            free(blocks);
            blocks = (void **)next;
        }
        while (!largeallocs.Empty()) free(largeallocs.Get());
    }

    // These are the most basic allocation functions, only useable if you know for sure
    // that your allocated size is <= MAXREUSESIZE (i.e. single objects).
    // They know their own size efficiently thanks to the pageheader, and are VERY fast.

    void *alloc_small(size_t size) {
        #ifdef PASSTHRUALLOC
            return alloc_large(size);
        #else
        assert(size <= MAXREUSESIZE);     // If you hit this, use alloc() below instead.
        int b = bucket((int)size);
        #ifdef COLLECT_STATS
            stats[b]++;
        #endif
        if (reuse[b].Empty()) return newpage(b);
        DLNodeRaw *r = reuse[b].Get();
        ppage(r)->refc++;
        return r;
        #endif
    }

    void dealloc_small(void *p) {
        #ifdef PASSTHRUALLOC
            dealloc_large(p);
            return;
        #endif
        PageHeader *page = ppage(p);
        #ifndef NDEBUG
            memset(p, 0xBA, page->size);
        #endif
        int b = page->size >> ALIGNBITS;
        reuse[b].InsertAfterThis((DLNodeRaw *)p);
        if (!--page->refc) freepage(page, page->size);
    }

    size_t size_of_small_allocation(const void *p) {
        return ppage(p)->size;
    }

    template<typename T> size_t size_of_small_allocation_typed(const T *p) {
        return size_of_small_allocation(p) / sizeof(T);
    }

    // Unlike the _small functions, these functions can deal with any size,
    // but require you to know the size you allocated upon deallocation.

    void *alloc(size_t size) {
        return size > MAXREUSESIZE ? alloc_large(size)
                                   : alloc_small(size);
    }

    void dealloc(void *p, size_t size) {
        if (size > MAXREUSESIZE) dealloc_large(p);
        else                     dealloc_small(p);
    }

    void *resize(void *p, size_t oldsize, size_t size) {
        void *np = alloc(size);
        memcpy(np, p, size>oldsize ? oldsize : size);
        dealloc(p, oldsize);
        return np;
    }

    // Versions of the above functions that track size for you, if you need drop-in free/malloc
    // style functionality.

    void *alloc_sized(size_t size) {
        size_t *p = (size_t *)alloc(size + sizeof(size_t));
        *p++ = size;  // Stores 2 sizes for big objects!
        return p;
    }

    void dealloc_sized(void *p) {
        size_t *t = (size_t *)p;
        size_t size = *--t;
        size += sizeof(size_t);
        dealloc(t, size);
    }

    static size_t size_of_allocation(const void *p) {
        return ((size_t *)p)[-1];
    }

    template<typename T> static size_t size_of_allocation_typed(const T *p) {
        return size_of_allocation(p) / sizeof(T);
    }

    void *resize_sized(void *p, size_t size) {
        void *np = alloc_sized(size);
        size_t oldsize = size_of_allocation(p);
        memcpy(np, p, size>oldsize ? oldsize : size);
        dealloc_sized(p);
        return np;
    }

    void *clone_sized(const void *p) {
        auto len = size_of_allocation(p);
        auto buf = alloc_sized(len);
        memcpy(buf, p, len);
        return buf;
    }

    // Convenient string allocation, dealloc with dealloc_sized.

    char *alloc_string_sized(string_view from) {
        auto len = from.size();
        char *buf = (char *)alloc_sized(len + 1);
        memcpy(buf, from.data(), len);
        buf[len] = 0;
        return buf;
    }

    static size_t size_of_string(const void *p) {
        return size_of_allocation(p) - 1;
    }

    // Typed helpers.

    template<typename T> T *alloc_obj_small() {     // T must fit inside MAXREUSESIZE.
        return (T *)alloc_small(sizeof(T));
    }

    template<typename T> T *create_obj_small() {
        return new (alloc_obj_small<T>()) T();
    }

    template<typename T> T *clone_obj_small(const T *from) {
        assert(from);
        auto to = (T *)alloc_small(sizeof(T));
        memcpy(to, from, sizeof(T));
        return to;
    }

    template<typename T> T *alloc_array(size_t numelems) {
        return (T *)alloc(sizeof(T) * numelems);
    }

    template<typename T> void dealloc_array(T *array, size_t numelems) {
        dealloc(array, sizeof(T) * numelems);
    }

    // Clones anything regardless of what it is, finds out size itself.
    void *clone_obj_small_unknown(const void *from) {
        assert(from);
        auto sz = size_of_small_allocation(from);
        auto to = alloc_small(sz);
        memcpy(to, from, sz);
        return to;
    }

    // Will even work with a derived class of T, assuming it was also allocated with
    // alloc_obj_small().
    template<typename T> void destruct_obj_small(T *obj) {
        obj->~T();
        dealloc_small(obj);
    }

    // Can get size of copied vector with size_of_allocation, and deallocate with dealloc_sized.
    template<typename T> T *vector_copy_sized(vector<T> from) {
        auto buf = (T *)alloc_sized(sizeof(T) * from.size());
        memcpy(buf, &from[0], sizeof(T) * from.size());
        return buf;
    }

    // For diagnostics and GC.

    size_t count_small_allocs() {
        size_t sum = 0;
        loopdllist(usedpages, h) sum += h->refc;
        return sum;
    }

    bool pointer_is_in_allocator(void *p) {
        for (auto b = blocks; b; b = (void **)*b) {
            if (p > b && p <= ((char *)b) + PAGEBLOCKSIZE) return true;
        }
        return false;
    }

    template<typename T> void findleaks(T leakcallback) {
        loopdllist(usedpages, h) {
            h->isfree = (char *)calloc(numobjs(h->size), 1);
        }
        for (int i = 0; i < MAXBUCKETS; i++) {
            loopdllist(reuse[i], n) {
                PageHeader *page = ppage(n);
                page->isfree[(((char *)n) - ((char *)(page + 1))) / (i * ALIGN)] = 1;
            }
        }
        loopdllist(usedpages, h) {
            for (int i = 0; i < numobjs(h->size); i++) {
                if (!h->isfree[i]) {
                    leakcallback(((char *)(h + 1)) + i * h->size);
                }
            }
            free(h->isfree);
            h->isfree = nullptr;
        }
        loopdllist(largeallocs, n) leakcallback(n + 1);
    }

    void printstats(bool full = false) {
        size_t totalwaste = 0;
        long long totalallocs = 0;
        for (int i = 0; i<MAXBUCKETS; i++) {
            size_t num = 0;
            loopdllist(reuse[i], n) num++;
            #ifdef COLLECT_STATS
                if (num || stats[i]) {
                    size_t waste = (i*ALIGN*num+512)/1024;
                    totalwaste += waste;
                    totalallocs += stats[i];
                    if (full || num) {
                        Output(OUTPUT_INFO, "bucket ", i * ALIGN, " -> freelist ", num, " (",
                                            waste, " k), ", stats[i], " total allocs");
                    }
                }
            #endif
        }
        int numfree = 0, numused = 0, numlarge = 0;
        loopdllist(freepages, h) numfree++;
        loopdllist(usedpages, h) numused++;
        loopdllist(largeallocs, n) numlarge++;
        if (full || numused || numlarge || totalallocs) {
            Output(OUTPUT_INFO, "totalwaste ", totalwaste, " k, pages ", numfree, " empty / ",
                                numused, " used, ", numlarge, " big alloc live, ", totalallocs,
                                " total allocs made, ", statbig, " big allocs made");
        }
    }
};


/* TODO / improvements:

If we could distinguish a big alloc pointer from a slab allocated one, we could read the size of
the block from the pageheader, giving the functionality of the _sized() functions without their
overhead to all allocations. Ways this could be done:
- Cooperate with the OS to have small pages mapped to a separate virtual memory area so we can do a
  simple pointer comparison
- Have magic numbers in the page header to distinguish it.. still not totally safe, and potentially
  access unused memory could be a pointer to itself. this guarantees that it can't be any other
  pointer.
  what are the chances that a float/int or other random data has the same value as the location its
  stored in? small, but still..
  of course could up the ante by having 2 such pointers, another one at the end of the page or
  whatever or could up it even further by having all big allocations have a magic number at the
  start of their buffer (not the page), so you'd first check that, then the page header... still
  not fool proof and many checks means slower
- Write a large block allocator that also always has page headers for the start of any allocation
  this means multi-page allocations can't reuse the remaining space in the last page. They also
  generally need to allocate their own pages, so unless you can acquire page aligned mem from the
  system, this would be anothe page of overhead and allocations that fit in a single page can
  possibly share a page (sizes roughly between MAXREUSESIZE and PAGESIZE). this category would
  share the page pool with the small allocs. Generally, large allocators that waste memory should
  make allocations that are as large as possible, then allow the client to query the size it
  actually got: this is useful for vectors etc that can actually use the extra space. This idea
  could even be useful for small allocs to some extend, especially if you start doing non-linear
  bucket sizes This all could be made specific to a programming language interpreter, since all
  large blocks are long strings or vectors, esp vectors, which make sense to make em just fill
  pages.
- Have a bit array for all pages of the entire 32bit address space that say if it is a small page
  block or not. at 2K pages, there's 2^21 pages, needing 2^18 bytes, i.e. 256K
  Doesn't work in 64bit mode, and 256K fixed overhead is not great but doable. Accessing that 256k
  might be a cache issue but the bulk of those pages sit really close. Can of course increase the
  page size to 4K or 8K, TC-Malloc thinks everything below 32k alloc is small, so we're probably
  being too careful. Or if you can get aligned memory
  from the OS, you could make this table really small, even if our pages are smaller than the OS
  ones.
*/
