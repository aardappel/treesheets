/*
 *  TCC - Tiny C Compiler - Support for -run switch
 *
 *  Copyright (c) 2001-2004 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "tcc.h"

/* only native compiler supports -run */
#ifdef TCC_IS_NATIVE

#ifdef CONFIG_TCC_BACKTRACE
typedef struct rt_context
{
    /* --> tccelf.c:tcc_add_btstub wants those below in that order: */
    union {
	struct {
    	    Stab_Sym *stab_sym, *stab_sym_end;
    	    char *stab_str;
	};
	struct {
    	    unsigned char *dwarf_line, *dwarf_line_end, *dwarf_line_str;
	};
    };
    addr_t dwarf;
    ElfW(Sym) *esym_start, *esym_end;
    char *elf_str;
    addr_t prog_base;
    void *bounds_start;
    struct rt_context *next;
    /* <-- */
    int num_callers;
    addr_t ip, fp, sp;
    void *top_func;
    jmp_buf jmp_buf;
    char do_jmp;
} rt_context;

static rt_context g_rtctxt;
static void set_exception_handler(void);
static int _rt_error(void *fp, void *ip, const char *fmt, va_list ap);
static void rt_exit(int code);
#endif /* CONFIG_TCC_BACKTRACE */

/* defined when included from lib/bt-exe.c */
#ifndef CONFIG_TCC_BACKTRACE_ONLY

#ifndef _WIN32
# include <sys/mman.h>
#endif

static void set_pages_executable(TCCState *s1, int mode, void *ptr, unsigned long length);
static int tcc_relocate_ex(TCCState *s1, void *ptr, addr_t ptr_diff);

#ifdef _WIN64
static void *win64_add_function_table(TCCState *s1);
static void win64_del_function_table(void *);
#endif

/* ------------------------------------------------------------- */
/* Do all relocations (needed before using tcc_get_symbol())
   Returns -1 on error. */

LIBTCCAPI int tcc_relocate(TCCState *s1, void *ptr)
{
    int size;
    addr_t ptr_diff = 0;

    if (TCC_RELOCATE_AUTO != ptr)
        return tcc_relocate_ex(s1, ptr, 0);

    size = tcc_relocate_ex(s1, NULL, 0);
    if (size < 0)
        return -1;

#ifdef HAVE_SELINUX
{
    /* Using mmap instead of malloc */
    void *prx;
    char tmpfname[] = "/tmp/.tccrunXXXXXX";
    int fd = mkstemp(tmpfname);
    unlink(tmpfname);
    ftruncate(fd, size);

    size = (size + (PAGESIZE-1)) & ~(PAGESIZE-1);
    ptr = mmap(NULL, size * 2, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    /* mmap RX memory at a fixed distance */
    prx = mmap((char*)ptr + size, size, PROT_READ|PROT_EXEC, MAP_SHARED|MAP_FIXED, fd, 0);
    if (ptr == MAP_FAILED || prx == MAP_FAILED)
	tcc_error("tccrun: could not map memory");
    ptr_diff = (char*)prx - (char*)ptr;
    close(fd);
    //printf("map %p %p %p\n", ptr, prx, (void*)ptr_diff);
}
#else
    ptr = tcc_malloc(size);
#endif
    tcc_relocate_ex(s1, ptr, ptr_diff); /* no more errors expected */
    dynarray_add(&s1->runtime_mem, &s1->nb_runtime_mem, (void*)(addr_t)size);
    dynarray_add(&s1->runtime_mem, &s1->nb_runtime_mem, ptr);
    return 0;
}

ST_FUNC void tcc_run_free(TCCState *s1)
{
    int i;

    for (i = 0; i < s1->nb_runtime_mem; i += 2) {
        unsigned size = (unsigned)(addr_t)s1->runtime_mem[i];
        void *ptr = s1->runtime_mem[i+1];
#ifdef HAVE_SELINUX
        munmap(ptr, size * 2);
#else
        /* unprotect memory to make it usable for malloc again */
        set_pages_executable(s1, 2, ptr, size);
#ifdef _WIN64
        win64_del_function_table(*(void**)ptr);
#endif
        tcc_free(ptr);
#endif
    }
    tcc_free(s1->runtime_mem);
}

static void run_cdtors(TCCState *s1, const char *start, const char *end,
                       int argc, char **argv, char **envp)
{
    void **a = (void **)get_sym_addr(s1, start, 0, 0);
    void **b = (void **)get_sym_addr(s1, end, 0, 0);
    while (a != b)
        ((void(*)(int, char **, char **))*a++)(argc, argv, envp);
}

/* launch the compiled program with the given arguments */
LIBTCCAPI int tcc_run(TCCState *s1, int argc, char **argv)
{
    int (*prog_main)(int, char **, char **), ret;
#ifdef CONFIG_TCC_BACKTRACE
    rt_context *rc = &g_rtctxt;
#endif

#if defined(__APPLE__) || defined(__FreeBSD__)
    char **envp = NULL;
#elif defined(__OpenBSD__) || defined(__NetBSD__)
    extern char **environ;
    char **envp = environ;
#else
    char **envp = environ;
#endif

    s1->runtime_main = s1->nostdlib ? "_start" : "main";
    if ((s1->dflag & 16) && (addr_t)-1 == get_sym_addr(s1, s1->runtime_main, 0, 1))
        return 0;
#ifdef CONFIG_TCC_BACKTRACE
    if (s1->do_debug)
        tcc_add_symbol(s1, "exit", rt_exit);
#endif
    if (tcc_relocate(s1, TCC_RELOCATE_AUTO) < 0)
        return -1;
    prog_main = (void*)get_sym_addr(s1, s1->runtime_main, 1, 1);

#ifdef CONFIG_TCC_BACKTRACE
    memset(rc, 0, sizeof *rc);
    if (s1->do_debug) {
        void *p;
	if (s1->dwarf) {
	    rc->dwarf_line = dwarf_line_section->data;
	    rc->dwarf_line_end = dwarf_line_section->data + dwarf_line_section->data_offset;
	    if (dwarf_line_str_section)
		rc->dwarf_line_str = dwarf_line_str_section->data;
	}
	else
	{
            rc->stab_sym = (Stab_Sym *)stab_section->data;
            rc->stab_sym_end = (Stab_Sym *)(stab_section->data + stab_section->data_offset);
            rc->stab_str = (char *)stab_section->link->data;
	}
        rc->dwarf = s1->dwarf;
        rc->esym_start = (ElfW(Sym) *)(symtab_section->data);
        rc->esym_end = (ElfW(Sym) *)(symtab_section->data + symtab_section->data_offset);
        rc->elf_str = (char *)symtab_section->link->data;
#if PTR_SIZE == 8
        rc->prog_base = text_section->sh_addr & 0xffffffff00000000ULL;
#endif
        rc->top_func = tcc_get_symbol(s1, "main");
        rc->num_callers = s1->rt_num_callers;
        rc->do_jmp = 1;
        if ((p = tcc_get_symbol(s1, "__rt_error")))
            *(void**)p = _rt_error;
#ifdef CONFIG_TCC_BCHECK
        if (s1->do_bounds_check) {
            rc->bounds_start = (void*)bounds_section->sh_addr;
            if ((p = tcc_get_symbol(s1, "__bound_init")))
                ((void(*)(void*,int))p)(rc->bounds_start, 1);
        }
#endif
        set_exception_handler();
    }
#endif

    errno = 0; /* clean errno value */
    fflush(stdout);
    fflush(stderr);
    /* These aren't C symbols, so don't need leading underscore handling.  */
    run_cdtors(s1, "__init_array_start", "__init_array_end", argc, argv, envp);
#ifdef CONFIG_TCC_BACKTRACE
    if (!rc->do_jmp || !(ret = setjmp(rc->jmp_buf)))
#endif
    {
        ret = prog_main(argc, argv, envp);
    }
    run_cdtors(s1, "__fini_array_start", "__fini_array_end", 0, NULL, NULL);
    if ((s1->dflag & 16) && ret)
        fprintf(s1->ppfp, "[returns %d]\n", ret), fflush(s1->ppfp);
    return ret;
}

#define DEBUG_RUNMEN 0

/* enable rx/ro/rw permissions */
#define CONFIG_RUNMEM_RO 1

#if CONFIG_RUNMEM_RO
# define PAGE_ALIGN PAGESIZE
#elif defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64
/* To avoid that x86 processors would reload cached instructions
   each time when data is written in the near, we need to make
   sure that code and data do not share the same 64 byte unit */
# define PAGE_ALIGN 64
#else
# define PAGE_ALIGN 1
#endif

/* relocate code. Return -1 on error, required size if ptr is NULL,
   otherwise copy code into buffer passed by the caller */
static int tcc_relocate_ex(TCCState *s1, void *ptr, addr_t ptr_diff)
{
    Section *s;
    unsigned offset, length, align, max_align, i, k, f;
    unsigned n, copy;
    addr_t mem, addr;

    if (NULL == ptr) {
        s1->nb_errors = 0;
#ifdef TCC_TARGET_PE
        pe_output_file(s1, NULL);
#else
        tcc_add_runtime(s1);
	resolve_common_syms(s1);
        build_got_entries(s1);
#endif
        if (s1->nb_errors)
            return -1;
    }

    offset = max_align = 0, mem = (addr_t)ptr;
#ifdef _WIN64
    offset += sizeof (void*); /* space for function_table pointer */
#endif
    copy = 0;
redo:
    for (k = 0; k < 3; ++k) { /* 0:rx, 1:ro, 2:rw sections */
        n = 0; addr = 0;
        for(i = 1; i < s1->nb_sections; i++) {
            static const char shf[] = {
                SHF_ALLOC|SHF_EXECINSTR, SHF_ALLOC, SHF_ALLOC|SHF_WRITE
                };
            s = s1->sections[i];
            if (shf[k] != (s->sh_flags & (SHF_ALLOC|SHF_WRITE|SHF_EXECINSTR)))
                continue;
            length = s->data_offset;
            if (copy) {
                if (addr == 0)
                    addr = s->sh_addr;
                n = (s->sh_addr - addr) + length;
                ptr = (void*)s->sh_addr;
                if (k == 0)
                    ptr = (void*)(s->sh_addr - ptr_diff);
                if (NULL == s->data || s->sh_type == SHT_NOBITS)
                    memset(ptr, 0, length);
                else
                    memcpy(ptr, s->data, length);
#ifdef _WIN64
                if (s == s1->uw_pdata)
                    *(void**)mem = win64_add_function_table(s1);
#endif
                if (s->data) {
                    tcc_free(s->data);
                    s->data = NULL;
                    s->data_allocated = 0;
                }
                s->data_offset = 0;
                continue;
            }
            align = s->sh_addralign - 1;
            if (++n == 1 && align < (PAGE_ALIGN - 1))
                align = (PAGE_ALIGN - 1);
            if (max_align < align)
                max_align = align;
            addr = k ? mem : mem + ptr_diff;
            offset += -(addr + offset) & align;
            s->sh_addr = mem ? addr + offset : 0;
            offset += length;
#if DEBUG_RUNMEN
            if (mem)
                printf("%d: %-16s %p  len %04x  align %04x\n",
                    k, s->name, (void*)s->sh_addr, length, align + 1);
#endif
        }
        if (copy) { /* set permissions */
            if (k == 0 && ptr_diff)
                continue; /* not with HAVE_SELINUX */
            f = k;
#if !CONFIG_RUNMEM_RO
            if (f != 0)
                continue;
            f = 3; /* change only SHF_EXECINSTR to rwx */
#endif
#if DEBUG_RUNMEN
            printf("protect %d %p %04x\n", f, (void*)addr, n);
#endif
            if (n)
                set_pages_executable(s1, f, (void*)addr, n);
        }
    }

    if (copy)
        return 0;

    /* relocate symbols */
    relocate_syms(s1, s1->symtab, !(s1->nostdlib));
    if (s1->nb_errors)
        return -1;
    if (0 == mem)
        return offset + max_align;

#ifdef TCC_TARGET_PE
    s1->pe_imagebase = mem;
#endif

    /* relocate sections */
#ifndef TCC_TARGET_PE
    relocate_plt(s1);
#endif
    relocate_sections(s1);
    copy = 1;
    goto redo;
}

/* ------------------------------------------------------------- */
/* allow to run code in memory */

static void set_pages_executable(TCCState *s1, int mode, void *ptr, unsigned long length)
{
#ifdef _WIN32
    static const unsigned char protect[] = {
        PAGE_EXECUTE_READ,
        PAGE_READONLY,
        PAGE_READWRITE,
        PAGE_EXECUTE_READWRITE
        };
    DWORD old;
    VirtualProtect(ptr, length, protect[mode], &old);
#else
    static const unsigned char protect[] = {
        PROT_READ | PROT_EXEC,
        PROT_READ,
        PROT_READ | PROT_WRITE,
        PROT_READ | PROT_WRITE | PROT_EXEC
        };
    addr_t start, end;
    start = (addr_t)ptr & ~(PAGESIZE - 1);
    end = (addr_t)ptr + length;
    end = (end + PAGESIZE - 1) & ~(PAGESIZE - 1);
    if (mprotect((void *)start, end - start, protect[mode]))
        tcc_error("mprotect failed: did you mean to configure --with-selinux?");

/* XXX: BSD sometimes dump core with bad system call */
# if (TCC_TARGET_ARM && !TARGETOS_BSD) || TCC_TARGET_ARM64
    if (mode == 0 || mode == 3) {
        void __clear_cache(void *beginning, void *end);
        __clear_cache(ptr, (char *)ptr + length);
    }
# endif

#endif
}

#ifdef _WIN64
static void *win64_add_function_table(TCCState *s1)
{
    void *p = NULL;
    if (s1->uw_pdata) {
        p = (void*)s1->uw_pdata->sh_addr;
        RtlAddFunctionTable(
            (RUNTIME_FUNCTION*)p,
            s1->uw_pdata->data_offset / sizeof (RUNTIME_FUNCTION),
            s1->pe_imagebase
            );
        s1->uw_pdata = NULL;
    }
    return p;
}

static void win64_del_function_table(void *p)
{
    if (p) {
        RtlDeleteFunctionTable((RUNTIME_FUNCTION*)p);
    }
}
#endif
#endif //ndef CONFIG_TCC_BACKTRACE_ONLY
/* ------------------------------------------------------------- */
#ifdef CONFIG_TCC_BACKTRACE

static int rt_vprintf(const char *fmt, va_list ap)
{
    int ret = vfprintf(stderr, fmt, ap);
    fflush(stderr);
    return ret;
}

static int rt_printf(const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = rt_vprintf(fmt, ap);
    va_end(ap);
    return r;
}

static char *rt_elfsym(rt_context *rc, addr_t wanted_pc, addr_t *func_addr)
{
    ElfW(Sym) *esym;
    for (esym = rc->esym_start + 1; esym < rc->esym_end; ++esym) {
        int type = ELFW(ST_TYPE)(esym->st_info);
        if ((type == STT_FUNC || type == STT_GNU_IFUNC)
            && wanted_pc >= esym->st_value
            && wanted_pc < esym->st_value + esym->st_size) {
            *func_addr = esym->st_value;
            return rc->elf_str + esym->st_name;
        }
    }
    return NULL;
}

#define INCLUDE_STACK_SIZE 32

/* print the position in the source file of PC value 'pc' by reading
   the stabs debug information */
static addr_t rt_printline (rt_context *rc, addr_t wanted_pc,
    const char *msg, const char *skip)
{
    char func_name[128];
    addr_t func_addr, last_pc, pc;
    const char *incl_files[INCLUDE_STACK_SIZE];
    int incl_index, last_incl_index, len, last_line_num, i;
    const char *str, *p;
    Stab_Sym *sym;

next:
    func_name[0] = '\0';
    func_addr = 0;
    incl_index = 0;
    last_pc = (addr_t)-1;
    last_line_num = 1;
    last_incl_index = 0;

    for (sym = rc->stab_sym + 1; sym < rc->stab_sym_end; ++sym) {
        str = rc->stab_str + sym->n_strx;
        pc = sym->n_value;

        switch(sym->n_type) {
        case N_SLINE:
            if (func_addr)
                goto rel_pc;
        case N_SO:
        case N_SOL:
            goto abs_pc;
        case N_FUN:
            if (sym->n_strx == 0) /* end of function */
                goto rel_pc;
        abs_pc:
#if PTR_SIZE == 8
            /* Stab_Sym.n_value is only 32bits */
            pc += rc->prog_base;
#endif
            goto check_pc;
        rel_pc:
            pc += func_addr;
        check_pc:
            if (pc >= wanted_pc && wanted_pc >= last_pc)
                goto found;
            break;
        }

        switch(sym->n_type) {
            /* function start or end */
        case N_FUN:
            if (sym->n_strx == 0)
                goto reset_func;
            p = strchr(str, ':');
            if (0 == p || (len = p - str + 1, len > sizeof func_name))
                len = sizeof func_name;
            pstrcpy(func_name, len, str);
            func_addr = pc;
            break;
            /* line number info */
        case N_SLINE:
            last_pc = pc;
            last_line_num = sym->n_desc;
            last_incl_index = incl_index;
            break;
            /* include files */
        case N_BINCL:
            if (incl_index < INCLUDE_STACK_SIZE)
                incl_files[incl_index++] = str;
            break;
        case N_EINCL:
            if (incl_index > 1)
                incl_index--;
            break;
            /* start/end of translation unit */
        case N_SO:
            incl_index = 0;
            if (sym->n_strx) {
                /* do not add path */
                len = strlen(str);
                if (len > 0 && str[len - 1] != '/')
                    incl_files[incl_index++] = str;
            }
        reset_func:
            func_name[0] = '\0';
            func_addr = 0;
            last_pc = (addr_t)-1;
            break;
            /* alternative file name (from #line or #include directives) */
        case N_SOL:
            if (incl_index)
                incl_files[incl_index-1] = str;
            break;
        }
    }

    func_name[0] = '\0';
    func_addr = 0;
    last_incl_index = 0;
    /* we try symtab symbols (no line number info) */
    p = rt_elfsym(rc, wanted_pc, &func_addr);
    if (p) {
        pstrcpy(func_name, sizeof func_name, p);
        goto found;
    }
    if ((rc = rc->next))
        goto next;
found:
    i = last_incl_index;
    if (i > 0) {
        str = incl_files[--i];
        if (skip[0] && strstr(str, skip))
            return (addr_t)-1;
        rt_printf("%s:%d: ", str, last_line_num);
    } else
        rt_printf("%08llx : ", (long long)wanted_pc);
    rt_printf("%s %s", msg, func_name[0] ? func_name : "???");
#if 0
    if (--i >= 0) {
        rt_printf(" (included from ");
        for (;;) {
            rt_printf("%s", incl_files[i]);
            if (--i < 0)
                break;
            rt_printf(", ");
        }
        rt_printf(")");
    }
#endif
    return func_addr;
}

/* ------------------------------------------------------------- */
/* rt_printline - dwarf version */

#define MAX_128	((8 * sizeof (long long) + 6) / 7)

#define DIR_TABLE_SIZE	(64)
#define FILE_TABLE_SIZE	(512)

#define	dwarf_read_1(ln,end) \
	((ln) < (end) ? *(ln)++ : 0)
#define	dwarf_read_2(ln,end) \
	((ln) + 2 < (end) ? (ln) += 2, read16le((ln) - 2) : 0)
#define	dwarf_read_4(ln,end) \
	((ln) + 4 < (end) ? (ln) += 4, read32le((ln) - 4) : 0)
#define	dwarf_read_8(ln,end) \
	((ln) + 8 < (end) ? (ln) += 8, read64le((ln) - 8) : 0)
#define	dwarf_ignore_type(ln, end) /* timestamp/size/md5/... */ \
	switch (entry_format[j].form) { \
	case DW_FORM_data1: (ln) += 1; break; \
	case DW_FORM_data2: (ln) += 2; break; \
	case DW_FORM_data4: (ln) += 3; break; \
	case DW_FORM_data8: (ln) += 8; break; \
	case DW_FORM_data16: (ln) += 16; break; \
	case DW_FORM_udata: dwarf_read_uleb128(&(ln), (end)); break; \
	default: goto next_line; \
	}

static unsigned long long
dwarf_read_uleb128(unsigned char **ln, unsigned char *end)
{
    unsigned char *cp = *ln;
    unsigned long long retval = 0;
    int i;

    for (i = 0; i < MAX_128; i++) {
	unsigned long long byte = dwarf_read_1(cp, end);

        retval |= (byte & 0x7f) << (i * 7);
	if ((byte & 0x80) == 0)
	    break;
    }
    *ln = cp;
    return retval;
}

static long long
dwarf_read_sleb128(unsigned char **ln, unsigned char *end)
{
    unsigned char *cp = *ln;
    long long retval = 0;
    int i;

    for (i = 0; i < MAX_128; i++) {
	unsigned long long byte = dwarf_read_1(cp, end);

        retval |= (byte & 0x7f) << (i * 7);
	if ((byte & 0x80) == 0) {
	    if ((byte & 0x40) && (i + 1) * 7 < 64)
		retval |= -1LL << ((i + 1) * 7);
	    break;
	}
    }
    *ln = cp;
    return retval;
}

static addr_t rt_printline_dwarf (rt_context *rc, addr_t wanted_pc,
    const char *msg, const char *skip)
{
    unsigned char *ln;
    unsigned char *cp;
    unsigned char *end;
    unsigned char *opcode_length;
    unsigned long long size;
    unsigned int length;
    unsigned char version;
    unsigned int min_insn_length;
    unsigned int max_ops_per_insn;
    int line_base;
    unsigned int line_range;
    unsigned int opcode_base;
    unsigned int opindex;
    unsigned int col;
    unsigned int i;
    unsigned int j;
    unsigned int len;
    unsigned long long value;
    struct {
	unsigned int type;
	unsigned int form;
    } entry_format[256];
    unsigned int dir_size;
#if 0
    char *dirs[DIR_TABLE_SIZE];
#endif
    unsigned int filename_size;
    struct dwarf_filename_struct {
        unsigned int dir_entry;
        char *name;
    } filename_table[FILE_TABLE_SIZE];
    addr_t last_pc;
    addr_t pc;
    addr_t func_addr;
    int line;
    char *filename;
    char *function;

next:
    ln = rc->dwarf_line;
    while (ln < rc->dwarf_line_end) {
	dir_size = 0;
	filename_size = 0;
        last_pc = 0;
        pc = 0;
        func_addr = 0;
        line = 1;
        filename = NULL;
        function = NULL;
	length = 4;
	size = dwarf_read_4(ln, rc->dwarf_line_end);
	if (size == 0xffffffffu) // dwarf 64
	    length = 8, size = dwarf_read_8(ln, rc->dwarf_line_end);
	end = ln + size;
	if (end < ln || end > rc->dwarf_line_end)
	    break;
	version = dwarf_read_2(ln, end);
	if (version >= 5)
	    ln += length + 2; // address size, segment selector, prologue Length
	else
	    ln += length; // prologue Length
	min_insn_length = dwarf_read_1(ln, end);
	if (version >= 4)
	    max_ops_per_insn = dwarf_read_1(ln, end);
	else
	    max_ops_per_insn = 1;
	ln++; // Initial value of 'is_stmt'
	line_base = dwarf_read_1(ln, end);
	line_base |= line_base >= 0x80 ? ~0xff : 0;
	line_range = dwarf_read_1(ln, end);
	opcode_base = dwarf_read_1(ln, end);
	opcode_length = ln;
	ln += opcode_base - 1;
	opindex = 0;
	if (version >= 5) {
	    col = dwarf_read_1(ln, end);
	    for (i = 0; i < col; i++) {
	        entry_format[i].type = dwarf_read_uleb128(&ln, end);
	        entry_format[i].form = dwarf_read_uleb128(&ln, end);
	    }
	    dir_size = dwarf_read_uleb128(&ln, end);
	    for (i = 0; i < dir_size; i++) {
		for (j = 0; j < col; j++) {
		    if (entry_format[j].type == DW_LNCT_path) {
		        if (entry_format[j].form != DW_FORM_line_strp)
			    goto next_line;
#if 0
		        value = length == 4 ? dwarf_read_4(ln, end)
					    : dwarf_read_8(ln, end);
		        if (i < DIR_TABLE_SIZE)
		            dirs[i] = (char *)rc->dwarf_line_str + value;
#else
			length == 4 ? dwarf_read_4(ln, end)
				    : dwarf_read_8(ln, end);
#endif
		    }
		    else 
			dwarf_ignore_type(ln, end);
		}
	    }
	    col = dwarf_read_1(ln, end);
	    for (i = 0; i < col; i++) {
	        entry_format[i].type = dwarf_read_uleb128(&ln, end);
	        entry_format[i].form = dwarf_read_uleb128(&ln, end);
	    }
	    filename_size = dwarf_read_uleb128(&ln, end);
	    for (i = 0; i < filename_size; i++)
		for (j = 0; j < col; j++) {
		    if (entry_format[j].type == DW_LNCT_path) {
			if (entry_format[j].form != DW_FORM_line_strp)
			    goto next_line;
			value = length == 4 ? dwarf_read_4(ln, end)
					    : dwarf_read_8(ln, end);
		        if (i < FILE_TABLE_SIZE)
		            filename_table[i].name =
				(char *)rc->dwarf_line_str + value;
	            }
		    else if (entry_format[j].type == DW_LNCT_directory_index) {
			switch (entry_format[j].form) {
			case DW_FORM_data1: value = dwarf_read_1(ln, end); break;
			case DW_FORM_data2: value = dwarf_read_2(ln, end); break;
			case DW_FORM_data4: value = dwarf_read_4(ln, end); break;
			case DW_FORM_udata: value = dwarf_read_uleb128(&ln, end); break;
			default: goto next_line;
			}
		        if (i < FILE_TABLE_SIZE)
		            filename_table[i].dir_entry = value;
		    }
		    else 
			dwarf_ignore_type(ln, end);
	    }
	}
	else {
	    while ((dwarf_read_1(ln, end))) {
#if 0
		if (++dir_size < DIR_TABLE_SIZE)
		    dirs[dir_size - 1] = (char *)ln - 1;
#endif
		while (dwarf_read_1(ln, end)) {}
	    }
	    while ((dwarf_read_1(ln, end))) {
		if (++filename_size < FILE_TABLE_SIZE) {
		    filename_table[filename_size - 1].name = (char *)ln - 1;
		    while (dwarf_read_1(ln, end)) {}
		    filename_table[filename_size - 1].dir_entry =
		        dwarf_read_uleb128(&ln, end);
		}
		else {
		    while (dwarf_read_1(ln, end)) {}
		    dwarf_read_uleb128(&ln, end);
		}
		dwarf_read_uleb128(&ln, end); // time
		dwarf_read_uleb128(&ln, end); // size
	    }
	}
	if (filename_size >= 1)
	    filename = filename_table[0].name;
	while (ln < end) {
	    last_pc = pc;
	    i = dwarf_read_1(ln, end);
	    if (i >= opcode_base) {
	        if (max_ops_per_insn == 1)
		    pc += ((i - opcode_base) / line_range) * min_insn_length;
		else {
		    pc += (opindex + (i - opcode_base) / line_range) /
			  max_ops_per_insn * min_insn_length;
		    opindex = (opindex + (i - opcode_base) / line_range) %
			       max_ops_per_insn;
		}
		i = (int)((i - opcode_base) % line_range) + line_base;
check_pc:
		if (pc >= wanted_pc && wanted_pc >= last_pc)
		    goto found;
		line += i;
	    }
	    else {
	        switch (i) {
	        case 0:
		    len = dwarf_read_uleb128(&ln, end);
		    cp = ln;
		    ln += len;
		    if (len == 0)
		        goto next_line;
		    switch (dwarf_read_1(cp, end)) {
		    case DW_LNE_end_sequence:
		        break;
		    case DW_LNE_set_address:
#if PTR_SIZE == 4
		        pc = dwarf_read_4(cp, end);
#else
		        pc = dwarf_read_8(cp, end);
#endif
		        opindex = 0;
		        break;
		    case DW_LNE_define_file: /* deprecated */
		        if (++filename_size < FILE_TABLE_SIZE) {
		            filename_table[filename_size - 1].name = (char *)ln - 1;
		            while (dwarf_read_1(ln, end)) {}
		            filename_table[filename_size - 1].dir_entry =
		                dwarf_read_uleb128(&ln, end);
		        }
		        else {
		            while (dwarf_read_1(ln, end)) {}
		            dwarf_read_uleb128(&ln, end);
		        }
		        dwarf_read_uleb128(&ln, end); // time
		        dwarf_read_uleb128(&ln, end); // size
		        break;
		    case DW_LNE_hi_user - 1:
		        function = (char *)cp;
		        func_addr = pc;
		        break;
		    default:
		        break;
		    }
		    break;
	        case DW_LNS_advance_pc:
		    if (max_ops_per_insn == 1)
		        pc += dwarf_read_uleb128(&ln, end) * min_insn_length;
		    else {
		        unsigned long long off = dwarf_read_uleb128(&ln, end);

		        pc += (opindex + off) / max_ops_per_insn *
			      min_insn_length;
		        opindex = (opindex + off) % max_ops_per_insn;
		    }
		    i = 0;
		    goto check_pc;
	        case DW_LNS_advance_line:
		    line += dwarf_read_sleb128(&ln, end);
		    break;
	        case DW_LNS_set_file:
		    i = dwarf_read_uleb128(&ln, end);
		    if (i < FILE_TABLE_SIZE && i < filename_size)
		        filename = filename_table[i].name;
		    break;
	        case DW_LNS_const_add_pc:
		    if (max_ops_per_insn ==  1)
		        pc += ((255 - opcode_base) / line_range) * min_insn_length;
		    else {
		        unsigned int off = (255 - opcode_base) / line_range;

		        pc += ((opindex + off) / max_ops_per_insn) *
			      min_insn_length;
		        opindex = (opindex + off) % max_ops_per_insn;
		    }
		    i = 0;
		    goto check_pc;
	        case DW_LNS_fixed_advance_pc:
		    i = dwarf_read_2(ln, end);
		    pc += i;
		    opindex = 0;
		    i = 0;
		    goto check_pc;
	        default:
		    for (j = 0; j < opcode_length[i - 1]; j++)
                        dwarf_read_uleb128 (&ln, end);
		    break;
		}
	    }
	}
next_line:
	ln = end;
    }

    filename = NULL;
    func_addr = 0;
    /* we try symtab symbols (no line number info) */
    function = rt_elfsym(rc, wanted_pc, &func_addr);
    if (function)
        goto found;
    if ((rc = rc->next))
        goto next;
found:
    if (filename) {
	if (skip[0] && strstr(filename, skip))
	    return (addr_t)-1;
	rt_printf("%s:%d: ", filename, line);
    }
    else
	rt_printf("0x%08llx : ", (long long)wanted_pc);
    rt_printf("%s %s", msg, function ? function : "???");
    return (addr_t)func_addr;
}
/* ------------------------------------------------------------- */

static int rt_get_caller_pc(addr_t *paddr, rt_context *rc, int level);

static int _rt_error(void *fp, void *ip, const char *fmt, va_list ap)
{
    rt_context *rc = &g_rtctxt;
    addr_t pc = 0;
    char skip[100];
    int i, level, ret, n;
    const char *a, *b, *msg;

    if (fp) {
        /* we're called from tcc_backtrace. */
        rc->fp = (addr_t)fp;
        rc->ip = (addr_t)ip;
        msg = "";
    } else {
        /* we're called from signal/exception handler */
        msg = "RUNTIME ERROR: ";
    }

    skip[0] = 0;
    /* If fmt is like "^file.c^..." then skip calls from 'file.c' */
    if (fmt[0] == '^' && (b = strchr(a = fmt + 1, fmt[0]))) {
        memcpy(skip, a, b - a), skip[b - a] = 0;
        fmt = b + 1;
    }

    n = rc->num_callers ? rc->num_callers : 6;
    for (i = level = 0; level < n; i++) {
        ret = rt_get_caller_pc(&pc, rc, i);
        a = "%s";
        if (ret != -1) {
	    if (rc->dwarf)
                pc = rt_printline_dwarf(rc, pc, level ? "by" : "at", skip);
	    else
                pc = rt_printline(rc, pc, level ? "by" : "at", skip);
            if (pc == (addr_t)-1)
                continue;
            a = ": %s";
        }
        if (level == 0) {
            rt_printf(a, msg);
            rt_vprintf(fmt, ap);
        } else if (ret == -1)
            break;
        rt_printf("\n");
        if (ret == -1 || (pc == (addr_t)rc->top_func && pc))
            break;
        ++level;
    }

    rc->ip = rc->fp = 0;
    return 0;
}

/* emit a run time error at position 'pc' */
static int rt_error(const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = _rt_error(0, 0, fmt, ap);
    va_end(ap);
    return ret;
}

static void rt_exit(int code)
{
    rt_context *rc = &g_rtctxt;
    if (rc->do_jmp)
        longjmp(rc->jmp_buf, code ? code : 256);
    exit(code);
}

/* ------------------------------------------------------------- */

#ifndef _WIN32
# include <signal.h>
# ifndef __OpenBSD__
#  include <sys/ucontext.h>
# endif
#else
# define ucontext_t CONTEXT
#endif

/* translate from ucontext_t* to internal rt_context * */
static void rt_getcontext(ucontext_t *uc, rt_context *rc)
{
#if defined _WIN64
    rc->ip = uc->Rip;
    rc->fp = uc->Rbp;
    rc->sp = uc->Rsp;
#elif defined _WIN32
    rc->ip = uc->Eip;
    rc->fp = uc->Ebp;
    rc->sp = uc->Esp;
#elif defined __i386__
# if defined(__APPLE__)
    rc->ip = uc->uc_mcontext->__ss.__eip;
    rc->fp = uc->uc_mcontext->__ss.__ebp;
# elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__DragonFly__)
    rc->ip = uc->uc_mcontext.mc_eip;
    rc->fp = uc->uc_mcontext.mc_ebp;
# elif defined(__dietlibc__)
    rc->ip = uc->uc_mcontext.eip;
    rc->fp = uc->uc_mcontext.ebp;
# elif defined(__NetBSD__)
    rc->ip = uc->uc_mcontext.__gregs[_REG_EIP];
    rc->fp = uc->uc_mcontext.__gregs[_REG_EBP];
# elif defined(__OpenBSD__)
    rc->ip = uc->sc_eip;
    rc->fp = uc->sc_ebp;
# elif !defined REG_EIP && defined EIP /* fix for glibc 2.1 */
    rc->ip = uc->uc_mcontext.gregs[EIP];
    rc->fp = uc->uc_mcontext.gregs[EBP];
# else
    rc->ip = uc->uc_mcontext.gregs[REG_EIP];
    rc->fp = uc->uc_mcontext.gregs[REG_EBP];
# endif
#elif defined(__x86_64__)
# if defined(__APPLE__)
    rc->ip = uc->uc_mcontext->__ss.__rip;
    rc->fp = uc->uc_mcontext->__ss.__rbp;
# elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__DragonFly__)
    rc->ip = uc->uc_mcontext.mc_rip;
    rc->fp = uc->uc_mcontext.mc_rbp;
# elif defined(__NetBSD__)
    rc->ip = uc->uc_mcontext.__gregs[_REG_RIP];
    rc->fp = uc->uc_mcontext.__gregs[_REG_RBP];
# elif defined(__OpenBSD__)
    rc->ip = uc->sc_rip;
    rc->fp = uc->sc_rbp;
# else
    rc->ip = uc->uc_mcontext.gregs[REG_RIP];
    rc->fp = uc->uc_mcontext.gregs[REG_RBP];
# endif
#elif defined(__arm__) && defined(__NetBSD__)
    rc->ip = uc->uc_mcontext.__gregs[_REG_PC];
    rc->fp = uc->uc_mcontext.__gregs[_REG_FP];
#elif defined(__arm__) && defined(__OpenBSD__)
    rc->ip = uc->sc_pc;
    rc->fp = uc->sc_r11;
#elif defined(__arm__) && defined(__FreeBSD__)
    rc->ip = uc->uc_mcontext.__gregs[_REG_PC];
    rc->fp = uc->uc_mcontext.__gregs[_REG_FP];
#elif defined(__arm__)
    rc->ip = uc->uc_mcontext.arm_pc;
    rc->fp = uc->uc_mcontext.arm_fp;
#elif defined(__aarch64__) && defined(__APPLE__)
    // see:
    // /Library/Developer/CommandLineTools/SDKs/MacOSX11.1.sdk/usr/include/mach/arm/_structs.h
    rc->ip = uc->uc_mcontext->__ss.__pc;
    rc->fp = uc->uc_mcontext->__ss.__fp;
#elif defined(__aarch64__) && defined(__FreeBSD__)
    rc->ip = uc->uc_mcontext.mc_gpregs.gp_elr; /* aka REG_PC */
    rc->fp = uc->uc_mcontext.mc_gpregs.gp_x[29];
#elif defined(__aarch64__) && defined(__NetBSD__)
    rc->ip = uc->uc_mcontext.__gregs[_REG_PC];
    rc->fp = uc->uc_mcontext.__gregs[_REG_FP];
#elif defined(__aarch64__) && defined(__OpenBSD__)
    rc->ip = uc->sc_elr;
    rc->fp = uc->sc_x[29];
#elif defined(__aarch64__)
    rc->ip = uc->uc_mcontext.pc;
    rc->fp = uc->uc_mcontext.regs[29];
#elif defined(__riscv) && defined(__OpenBSD__)
    rc->ip = uc->sc_sepc;
    rc->fp = uc->sc_s[0];
#elif defined(__riscv)
    rc->ip = uc->uc_mcontext.__gregs[REG_PC];
    rc->fp = uc->uc_mcontext.__gregs[REG_S0];
#endif
}

/* ------------------------------------------------------------- */
#ifndef _WIN32
/* signal handler for fatal errors */
static void sig_error(int signum, siginfo_t *siginf, void *puc)
{
    rt_context *rc = &g_rtctxt;
    rt_getcontext(puc, rc);

    switch(signum) {
    case SIGFPE:
        switch(siginf->si_code) {
        case FPE_INTDIV:
        case FPE_FLTDIV:
            rt_error("division by zero");
            break;
        default:
            rt_error("floating point exception");
            break;
        }
        break;
    case SIGBUS:
    case SIGSEGV:
        rt_error("invalid memory access");
        break;
    case SIGILL:
        rt_error("illegal instruction");
        break;
    case SIGABRT:
        rt_error("abort() called");
        break;
    default:
        rt_error("caught signal %d", signum);
        break;
    }
    rt_exit(255);
}

#ifndef SA_SIGINFO
# define SA_SIGINFO 0x00000004u
#endif

/* Generate a stack backtrace when a CPU exception occurs. */
static void set_exception_handler(void)
{
    struct sigaction sigact;
    /* install TCC signal handlers to print debug info on fatal
       runtime errors */
    sigemptyset (&sigact.sa_mask);
    sigact.sa_flags = SA_SIGINFO | SA_RESETHAND;
#if 0//def SIGSTKSZ // this causes signals not to work at all on some (older) linuxes
    sigact.sa_flags |= SA_ONSTACK;
#endif
    sigact.sa_sigaction = sig_error;
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGFPE, &sigact, NULL);
    sigaction(SIGILL, &sigact, NULL);
    sigaction(SIGSEGV, &sigact, NULL);
    sigaction(SIGBUS, &sigact, NULL);
    sigaction(SIGABRT, &sigact, NULL);
#if 0//def SIGSTKSZ
    /* This allows stack overflow to be reported instead of a SEGV */
    {
        stack_t ss;
        static unsigned char stack[SIGSTKSZ] __attribute__((aligned(16)));

        ss.ss_sp = stack;
        ss.ss_size = SIGSTKSZ;
        ss.ss_flags = 0;
        sigaltstack(&ss, NULL);
    }
#endif
}

#else /* WIN32 */

/* signal handler for fatal errors */
static long __stdcall cpu_exception_handler(EXCEPTION_POINTERS *ex_info)
{
    rt_context *rc = &g_rtctxt;
    unsigned code;
    rt_getcontext(ex_info->ContextRecord, rc);

    switch (code = ex_info->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
	rt_error("invalid memory access");
        break;
    case EXCEPTION_STACK_OVERFLOW:
        rt_error("stack overflow");
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        rt_error("division by zero");
        break;
    case EXCEPTION_BREAKPOINT:
    case EXCEPTION_SINGLE_STEP:
        rc->ip = *(addr_t*)rc->sp;
        rt_error("breakpoint/single-step exception:");
        return EXCEPTION_CONTINUE_SEARCH;
    default:
        rt_error("caught exception %08x", code);
        break;
    }
    if (rc->do_jmp)
        rt_exit(255);
    return EXCEPTION_EXECUTE_HANDLER;
}

/* Generate a stack backtrace when a CPU exception occurs. */
static void set_exception_handler(void)
{
    SetUnhandledExceptionFilter(cpu_exception_handler);
}

#endif

/* ------------------------------------------------------------- */
/* return the PC at frame level 'level'. Return negative if not found */
#if defined(__i386__) || defined(__x86_64__)
static int rt_get_caller_pc(addr_t *paddr, rt_context *rc, int level)
{
    addr_t ip, fp;
    if (level == 0) {
        ip = rc->ip;
    } else {
        ip = 0;
        fp = rc->fp;
        while (--level) {
            /* XXX: check address validity with program info */
            if (fp <= 0x1000)
                break;
            fp = ((addr_t *)fp)[0];
        }
        if (fp > 0x1000)
            ip = ((addr_t *)fp)[1];
    }
    if (ip <= 0x1000)
        return -1;
    *paddr = ip;
    return 0;
}

#elif defined(__arm__)
static int rt_get_caller_pc(addr_t *paddr, rt_context *rc, int level)
{
    /* XXX: only supports linux/bsd */
#if !defined(__linux__) && \
    !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__)
    return -1;
#else
    if (level == 0) {
        *paddr = rc->ip;
    } else {
        addr_t fp = rc->fp;
        while (--level)
            fp = ((addr_t *)fp)[0];
        *paddr = ((addr_t *)fp)[2];
    }
    return 0;
#endif
}

#elif defined(__aarch64__)
static int rt_get_caller_pc(addr_t *paddr, rt_context *rc, int level)
{
    if (level == 0) {
        *paddr = rc->ip;
    } else {
        addr_t *fp = (addr_t*)rc->fp;
        while (--level)
            fp = (addr_t *)fp[0];
        *paddr = fp[1];
    }
    return 0;
}

#elif defined(__riscv)
static int rt_get_caller_pc(addr_t *paddr, rt_context *rc, int level)
{
    if (level == 0) {
        *paddr = rc->ip;
    } else {
        addr_t *fp = (addr_t*)rc->fp;
        while (--level && fp >= (addr_t*)0x1000)
            fp = (addr_t *)fp[-2];
        if (fp < (addr_t*)0x1000)
          return -1;
        *paddr = fp[-1];
    }
    return 0;
}

#else
#warning add arch specific rt_get_caller_pc()
static int rt_get_caller_pc(addr_t *paddr, rt_context *rc, int level)
{
    return -1;
}

#endif
#endif /* CONFIG_TCC_BACKTRACE */
/* ------------------------------------------------------------- */
#ifdef CONFIG_TCC_STATIC

/* dummy function for profiling */
ST_FUNC void *dlopen(const char *filename, int flag)
{
    return NULL;
}

ST_FUNC void dlclose(void *p)
{
}

ST_FUNC const char *dlerror(void)
{
    return "error";
}

typedef struct TCCSyms {
    char *str;
    void *ptr;
} TCCSyms;


/* add the symbol you want here if no dynamic linking is done */
static TCCSyms tcc_syms[] = {
#if !defined(CONFIG_TCCBOOT)
#define TCCSYM(a) { #a, &a, },
    TCCSYM(printf)
    TCCSYM(fprintf)
    TCCSYM(fopen)
    TCCSYM(fclose)
#undef TCCSYM
#endif
    { NULL, NULL },
};

ST_FUNC void *dlsym(void *handle, const char *symbol)
{
    TCCSyms *p;
    p = tcc_syms;
    while (p->str != NULL) {
        if (!strcmp(p->str, symbol))
            return p->ptr;
        p++;
    }
    return NULL;
}

#endif /* CONFIG_TCC_STATIC */
#endif /* TCC_IS_NATIVE */
/* ------------------------------------------------------------- */
