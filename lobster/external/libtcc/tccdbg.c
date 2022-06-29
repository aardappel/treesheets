/*
 *  TCC - Tiny C Compiler
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

/* stab debug support */

static const struct {
  int type;
  int size;
  int encoding;
  const char *name;
} default_debug[] = {
    {   VT_INT, 4, DW_ATE_signed, "int:t1=r1;-2147483648;2147483647;" },
    {   VT_BYTE, 1, DW_ATE_signed_char, "char:t2=r2;0;127;" },
#if LONG_SIZE == 4
    {   VT_LONG | VT_INT, 4, DW_ATE_signed, "long int:t3=r3;-2147483648;2147483647;" },
#else
    {   VT_LLONG | VT_LONG, 8, DW_ATE_signed, "long int:t3=r3;-9223372036854775808;9223372036854775807;" },
#endif
    {   VT_INT | VT_UNSIGNED, 4, DW_ATE_unsigned, "unsigned int:t4=r4;0;037777777777;" },
#if LONG_SIZE == 4
    {   VT_LONG | VT_INT | VT_UNSIGNED, 4, DW_ATE_unsigned, "long unsigned int:t5=r5;0;037777777777;" },
#else
    /* use octal instead of -1 so size_t works (-gstabs+ in gcc) */
    {   VT_LLONG | VT_LONG | VT_UNSIGNED, 8, DW_ATE_unsigned, "long unsigned int:t5=r5;0;01777777777777777777777;" },
#endif
    {   VT_QLONG, 16, DW_ATE_signed, "__int128:t6=r6;0;-1;" },
    {   VT_QLONG | VT_UNSIGNED, 16, DW_ATE_unsigned, "__int128 unsigned:t7=r7;0;-1;" },
    {   VT_LLONG, 8, DW_ATE_signed, "long long int:t8=r8;-9223372036854775808;9223372036854775807;" },
    {   VT_LLONG | VT_UNSIGNED, 8, DW_ATE_unsigned, "long long unsigned int:t9=r9;0;01777777777777777777777;" },
    {   VT_SHORT, 2, DW_ATE_signed, "short int:t10=r10;-32768;32767;" },
    {   VT_SHORT | VT_UNSIGNED, 2, DW_ATE_unsigned, "short unsigned int:t11=r11;0;65535;" },
    {   VT_BYTE | VT_DEFSIGN, 1, DW_ATE_signed_char, "signed char:t12=r12;-128;127;" },
    {   VT_BYTE | VT_DEFSIGN | VT_UNSIGNED, 1, DW_ATE_unsigned_char, "unsigned char:t13=r13;0;255;" },
    {   VT_FLOAT, 4, DW_ATE_float, "float:t14=r1;4;0;" },
    {   VT_DOUBLE, 8, DW_ATE_float, "double:t15=r1;8;0;" },
#ifdef TCC_USING_DOUBLE_FOR_LDOUBLE
    {   VT_DOUBLE | VT_LONG, 8, DW_ATE_float, "long double:t16=r1;8;0;" },
#else
    {   VT_LDOUBLE, 16, DW_ATE_float, "long double:t16=r1;16;0;" },
#endif
    {   -1, -1, -1, "_Float32:t17=r1;4;0;" },
    {   -1, -1, -1, "_Float64:t18=r1;8;0;" },
    {   -1, -1, -1, "_Float128:t19=r1;16;0;" },
    {   -1, -1, -1, "_Float32x:t20=r1;8;0;" },
    {   -1, -1, -1, "_Float64x:t21=r1;16;0;" },
    {   -1, -1, -1, "_Decimal32:t22=r1;4;0;" },
    {   -1, -1, -1, "_Decimal64:t23=r1;8;0;" },
    {   -1, -1, -1, "_Decimal128:t24=r1;16;0;" },
    /* if default char is unsigned */
    {   VT_BYTE | VT_UNSIGNED, 1, DW_ATE_unsigned_char, "unsigned char:t25=r25;0;255;" },
    /* boolean type */
    {   VT_BOOL, 1, DW_ATE_boolean, "bool:t26=r26;0;255;" },
#if LONG_SIZE == 4
    {   VT_VOID, 1, DW_ATE_unsigned_char, "void:t27=27" },
#else 
    /* bitfields use these */
    {   VT_LONG | VT_INT, 8, DW_ATE_signed, "long int:t27=r27;-9223372036854775808;9223372036854775807;" },
    {   VT_LONG | VT_INT | VT_UNSIGNED, 8, DW_ATE_unsigned, "long unsigned int:t28=r28;0;01777777777777777777777;" },
    {   VT_VOID, 1, DW_ATE_unsigned_char, "void:t29=29" },
#endif
};

#define	N_DEFAULT_DEBUG	(sizeof (default_debug) / sizeof (default_debug[0]))

/* dwarf debug */

#define	DWARF_LINE_BASE				-5
#define	DWARF_LINE_RANGE			14
#define	DWARF_OPCODE_BASE			13

#if defined TCC_TARGET_ARM64
#define	DWARF_MIN_INSTR_LEN			4
#elif defined TCC_TARGET_ARM
#define	DWARF_MIN_INSTR_LEN			2
#else
#define	DWARF_MIN_INSTR_LEN			1
#endif

#define	DWARF_ABBREV_COMPILE_UNIT		1
#define	DWARF_ABBREV_BASE_TYPE			2
#define	DWARF_ABBREV_VARIABLE_EXTERNAL		3
#define	DWARF_ABBREV_VARIABLE_STATIC		4
#define	DWARF_ABBREV_VARIABLE_LOCAL		5
#define	DWARF_ABBREV_FORMAL_PARAMETER		6
#define	DWARF_ABBREV_POINTER			7
#define	DWARF_ABBREV_ARRAY_TYPE			8
#define	DWARF_ABBREV_SUBRANGE_TYPE		9
#define	DWARF_ABBREV_TYPEDEF			10
#define	DWARF_ABBREV_ENUMERATOR_SIGNED		11
#define	DWARF_ABBREV_ENUMERATOR_UNSIGNED	12
#define	DWARF_ABBREV_ENUMERATION_TYPE		13
#define	DWARF_ABBREV_MEMBER			14
#define	DWARF_ABBREV_MEMBER_BF			15
#define	DWARF_ABBREV_STRUCTURE_TYPE		16
#define	DWARF_ABBREV_UNION_TYPE			17
#define	DWARF_ABBREV_SUBPROGRAM_EXTERNAL	18
#define	DWARF_ABBREV_SUBPROGRAM_STATIC		19
#define	DWARF_ABBREV_LEXICAL_BLOCK		20
#define	DWARF_ABBREV_SUBROUTINE_TYPE		21
#define	DWARF_ABBREV_FORMAL_PARAMETER2		22

/* all entries should have been generated with dwarf_uleb128 except
   has_children. All values are currently below 128 so this currently
   works.  */
static const unsigned char dwarf_abbrev_init[] = {
    DWARF_ABBREV_COMPILE_UNIT, DW_TAG_compile_unit, 1,
          DW_AT_producer, DW_FORM_strp,
          DW_AT_language, DW_FORM_data1,
          DW_AT_name, DW_FORM_line_strp,
          DW_AT_comp_dir, DW_FORM_line_strp,
          DW_AT_low_pc, DW_FORM_addr,
#if PTR_SIZE == 4
          DW_AT_high_pc, DW_FORM_data4,
#else
          DW_AT_high_pc, DW_FORM_data8,
#endif
          DW_AT_stmt_list, DW_FORM_sec_offset,
          0, 0,
    DWARF_ABBREV_BASE_TYPE, DW_TAG_base_type, 0,
          DW_AT_byte_size, DW_FORM_udata,
          DW_AT_encoding, DW_FORM_data1,
          DW_AT_name, DW_FORM_strp,
          0, 0,
    DWARF_ABBREV_VARIABLE_EXTERNAL, DW_TAG_variable, 0,
          DW_AT_name, DW_FORM_strp,
          DW_AT_decl_file, DW_FORM_udata,
          DW_AT_decl_line, DW_FORM_udata,
          DW_AT_type, DW_FORM_ref4,
          DW_AT_external, DW_FORM_flag,
          DW_AT_location, DW_FORM_exprloc,
          0, 0,
    DWARF_ABBREV_VARIABLE_STATIC, DW_TAG_variable, 0,
          DW_AT_name, DW_FORM_strp,
          DW_AT_decl_file, DW_FORM_udata,
          DW_AT_decl_line, DW_FORM_udata,
          DW_AT_type, DW_FORM_ref4,
          DW_AT_location, DW_FORM_exprloc,
          0, 0,
    DWARF_ABBREV_VARIABLE_LOCAL, DW_TAG_variable, 0,
          DW_AT_name, DW_FORM_strp,
          DW_AT_type, DW_FORM_ref4,
          DW_AT_location, DW_FORM_exprloc,
          0, 0,
    DWARF_ABBREV_FORMAL_PARAMETER, DW_TAG_formal_parameter, 0,
          DW_AT_name, DW_FORM_strp,
          DW_AT_type, DW_FORM_ref4,
          DW_AT_location, DW_FORM_exprloc,
          0, 0,
    DWARF_ABBREV_POINTER, DW_TAG_pointer_type, 0,
          DW_AT_byte_size, DW_FORM_data1,
          DW_AT_type, DW_FORM_ref4,
          0, 0,
    DWARF_ABBREV_ARRAY_TYPE, DW_TAG_array_type, 1,
          DW_AT_type, DW_FORM_ref4,
          DW_AT_sibling, DW_FORM_ref4,
          0, 0,
    DWARF_ABBREV_SUBRANGE_TYPE, DW_TAG_subrange_type, 0,
          DW_AT_type, DW_FORM_ref4,
          DW_AT_upper_bound, DW_FORM_udata,
          0, 0,
    DWARF_ABBREV_TYPEDEF, DW_TAG_typedef, 0,
          DW_AT_name, DW_FORM_strp,
          DW_AT_decl_file, DW_FORM_udata,
          DW_AT_decl_line, DW_FORM_udata,
          DW_AT_type, DW_FORM_ref4,
          0, 0,
    DWARF_ABBREV_ENUMERATOR_SIGNED, DW_TAG_enumerator, 0,
          DW_AT_name, DW_FORM_strp,
          DW_AT_const_value, DW_FORM_sdata,
          0, 0,
    DWARF_ABBREV_ENUMERATOR_UNSIGNED, DW_TAG_enumerator, 0,
          DW_AT_name, DW_FORM_strp,
          DW_AT_const_value, DW_FORM_udata,
          0, 0,
    DWARF_ABBREV_ENUMERATION_TYPE, DW_TAG_enumeration_type, 1,
          DW_AT_name, DW_FORM_strp,
          DW_AT_encoding, DW_FORM_data1,
          DW_AT_byte_size, DW_FORM_data1,
          DW_AT_type, DW_FORM_ref4,
          DW_AT_decl_file, DW_FORM_udata,
          DW_AT_decl_line, DW_FORM_udata,
          DW_AT_sibling, DW_FORM_ref4,
          0, 0,
    DWARF_ABBREV_MEMBER, DW_TAG_member, 0,
          DW_AT_name, DW_FORM_strp,
          DW_AT_decl_file, DW_FORM_udata,
          DW_AT_decl_line, DW_FORM_udata,
          DW_AT_type, DW_FORM_ref4,
          DW_AT_data_member_location, DW_FORM_udata,
          0, 0,
    DWARF_ABBREV_MEMBER_BF, DW_TAG_member, 0,
          DW_AT_name, DW_FORM_strp,
          DW_AT_decl_file, DW_FORM_udata,
          DW_AT_decl_line, DW_FORM_udata,
          DW_AT_type, DW_FORM_ref4,
          DW_AT_bit_size, DW_FORM_udata,
          DW_AT_data_bit_offset, DW_FORM_udata,
          0, 0,
    DWARF_ABBREV_STRUCTURE_TYPE, DW_TAG_structure_type, 1,
          DW_AT_name, DW_FORM_strp,
          DW_AT_byte_size, DW_FORM_udata,
          DW_AT_decl_file, DW_FORM_udata,
          DW_AT_decl_line, DW_FORM_udata,
          DW_AT_sibling, DW_FORM_ref4,
          0, 0,
    DWARF_ABBREV_UNION_TYPE, DW_TAG_union_type, 1,
          DW_AT_name, DW_FORM_strp,
          DW_AT_byte_size, DW_FORM_udata,
          DW_AT_decl_file, DW_FORM_udata,
          DW_AT_decl_line, DW_FORM_udata,
          DW_AT_sibling, DW_FORM_ref4,
          0, 0,
    DWARF_ABBREV_SUBPROGRAM_EXTERNAL, DW_TAG_subprogram, 1,
          DW_AT_external, DW_FORM_flag,
          DW_AT_name, DW_FORM_strp,
          DW_AT_decl_file, DW_FORM_udata,
          DW_AT_decl_line, DW_FORM_udata,
          DW_AT_type, DW_FORM_ref4,
          DW_AT_low_pc, DW_FORM_addr,
#if PTR_SIZE == 4
          DW_AT_high_pc, DW_FORM_data4,
#else
          DW_AT_high_pc, DW_FORM_data8,
#endif
          DW_AT_sibling, DW_FORM_ref4,
	  DW_AT_frame_base, DW_FORM_exprloc,
          0, 0,
    DWARF_ABBREV_SUBPROGRAM_STATIC, DW_TAG_subprogram, 1,
          DW_AT_name, DW_FORM_strp,
          DW_AT_decl_file, DW_FORM_udata,
          DW_AT_decl_line, DW_FORM_udata,
          DW_AT_type, DW_FORM_ref4,
          DW_AT_low_pc, DW_FORM_addr,
#if PTR_SIZE == 4
          DW_AT_high_pc, DW_FORM_data4,
#else
          DW_AT_high_pc, DW_FORM_data8,
#endif
          DW_AT_sibling, DW_FORM_ref4,
	  DW_AT_frame_base, DW_FORM_exprloc,
          0, 0,
    DWARF_ABBREV_LEXICAL_BLOCK, DW_TAG_lexical_block, 1,
          DW_AT_low_pc, DW_FORM_addr,
#if PTR_SIZE == 4
          DW_AT_high_pc, DW_FORM_data4,
#else
          DW_AT_high_pc, DW_FORM_data8,
#endif
          0, 0,
    DWARF_ABBREV_SUBROUTINE_TYPE, DW_TAG_subroutine_type, 1,
          DW_AT_type, DW_FORM_ref4,
          DW_AT_sibling, DW_FORM_ref4,
          0, 0,
    DWARF_ABBREV_FORMAL_PARAMETER2, DW_TAG_formal_parameter, 0,
          DW_AT_type, DW_FORM_ref4,
          0, 0,
  0
};

static const unsigned char dwarf_line_opcodes[] = {
    0 ,1 ,1 ,1 ,1 ,0 ,0 ,0 ,1 ,0 ,0 ,1
};

/* ------------------------------------------------------------------------- */
/* debug state */

struct _tccdbg {

    int last_line_num, new_file;
    int section_sym;

    int debug_next_type;

    struct _debug_hash {
        int debug_type;
        Sym *type;
    } *debug_hash;

    struct _debug_anon_hash {
        Sym *type;
        int n_debug_type;
        int *debug_type;
    } *debug_anon_hash;

    int n_debug_hash;
    int n_debug_anon_hash;

    struct _debug_info {
        int start;
        int end;
        int n_sym;
        struct debug_sym {
            int type;
            unsigned long value;
            char *str;
            Section *sec;
            int sym_index;
            int info;
            int file;
            int line;
        } *sym;
        struct _debug_info *child, *next, *last, *parent;
    } *debug_info, *debug_info_root;

    struct {
        int info;
        int abbrev;
        int line;
        int str;
        int line_str;
    } dwarf_sym;

    struct {
        int start;
        int dir_size;
        char **dir_table;
        int filename_size;
        struct dwarf_filename_struct {
            int dir_entry;
            char *name;
        } *filename_table;
        int line_size;
        int line_max_size;
        unsigned char *line_data;
        int cur_file;
        int last_file;
        int last_pc;
        int last_line;
    } dwarf_line;

    struct {
        int start;
        Sym *func;
        int line;
        int base_type_used[N_DEFAULT_DEBUG];
    } dwarf_info;

    /* test coverage */
    struct {
        unsigned long offset;
        unsigned long last_file_name;
        unsigned long last_func_name;
        int ind;
        int line;
    } tcov_data;

};

#define last_line_num       s1->dState->last_line_num
#define new_file            s1->dState->new_file
#define section_sym         s1->dState->section_sym
#define debug_next_type     s1->dState->debug_next_type
#define debug_hash          s1->dState->debug_hash
#define debug_anon_hash     s1->dState->debug_anon_hash
#define n_debug_hash        s1->dState->n_debug_hash
#define n_debug_anon_hash   s1->dState->n_debug_anon_hash
#define debug_info          s1->dState->debug_info
#define debug_info_root     s1->dState->debug_info_root
#define dwarf_sym           s1->dState->dwarf_sym
#define dwarf_line          s1->dState->dwarf_line
#define dwarf_info          s1->dState->dwarf_info
#define tcov_data           s1->dState->tcov_data

/* ------------------------------------------------------------------------- */
static void put_stabs(TCCState *s1, const char *str, int type, int other,
    int desc, unsigned long value);

ST_FUNC void tcc_debug_new(TCCState *s1)
{
    int shf = 0;
    if (!s1->dState)
        s1->dState = tcc_mallocz(sizeof *s1->dState);
#ifdef CONFIG_TCC_BACKTRACE
    /* include stab info with standalone backtrace support */
    if (s1->do_backtrace
        && (s1->output_type == TCC_OUTPUT_EXE
         || s1->output_type == TCC_OUTPUT_DLL)
         )
        shf = SHF_ALLOC | SHF_WRITE; // SHF_WRITE needed for musl/SELINUX
#endif
    if (s1->dwarf) {
        s1->dwlo = s1->nb_sections;
        dwarf_info_section =
	    new_section(s1, ".debug_info", SHT_PROGBITS, shf);
        dwarf_abbrev_section =
	    new_section(s1, ".debug_abbrev", SHT_PROGBITS, shf);
        dwarf_line_section =
	    new_section(s1, ".debug_line", SHT_PROGBITS, shf);
        dwarf_aranges_section =
	    new_section(s1, ".debug_aranges", SHT_PROGBITS, shf);
	shf |= SHF_MERGE | SHF_STRINGS;
        dwarf_str_section =
	    new_section(s1, ".debug_str", SHT_PROGBITS, shf);
	dwarf_str_section->sh_entsize = 1;
	dwarf_info_section->sh_addralign =
	dwarf_abbrev_section->sh_addralign =
	dwarf_line_section->sh_addralign =
	dwarf_aranges_section->sh_addralign =
	dwarf_str_section->sh_addralign = 1;
	if (s1->dwarf >= 5) {
            dwarf_line_str_section =
	        new_section(s1, ".debug_line_str", SHT_PROGBITS, shf);
	    dwarf_line_str_section->sh_entsize = 1;
	    dwarf_line_str_section->sh_addralign = 1;
	}
        s1->dwhi = s1->nb_sections;
    }
    else
    {
        stab_section = new_section(s1, ".stab", SHT_PROGBITS, shf);
        stab_section->sh_entsize = sizeof(Stab_Sym);
        stab_section->sh_addralign = sizeof ((Stab_Sym*)0)->n_value;
        stab_section->link = new_section(s1, ".stabstr", SHT_STRTAB, shf);
        /* put first entry */
        put_stabs(s1, "", 0, 0, 0, 0);
    }
}

/* put stab debug information */
static void put_stabs(TCCState *s1, const char *str, int type, int other, int desc,
                      unsigned long value)
{
    Stab_Sym *sym;

    unsigned offset;
    if (type == N_SLINE
        && (offset = stab_section->data_offset)
        && (sym = (Stab_Sym*)(stab_section->data + offset) - 1)
        && sym->n_type == type
        && sym->n_value == value) {
        /* just update line_number in previous entry */
        sym->n_desc = desc;
        return;
    }

    sym = section_ptr_add(stab_section, sizeof(Stab_Sym));
    if (str) {
        sym->n_strx = put_elf_str(stab_section->link, str);
    } else {
        sym->n_strx = 0;
    }
    sym->n_type = type;
    sym->n_other = other;
    sym->n_desc = desc;
    sym->n_value = value;
}

static void put_stabs_r(TCCState *s1, const char *str, int type, int other, int desc,
                        unsigned long value, Section *sec, int sym_index)
{
    put_elf_reloc(symtab_section, stab_section,
                  stab_section->data_offset + 8,
                  sizeof ((Stab_Sym*)0)->n_value == PTR_SIZE ? R_DATA_PTR : R_DATA_32,
                  sym_index);
    put_stabs(s1, str, type, other, desc, value);
}

static void put_stabn(TCCState *s1, int type, int other, int desc, int value)
{
    put_stabs(s1, NULL, type, other, desc, value);
}

/* ------------------------------------------------------------------------- */
#define	dwarf_data1(s,data) \
	{ unsigned char *p = section_ptr_add((s), 1); *p = (data); }
#define	dwarf_data2(s,data) \
	write16le(section_ptr_add((s), 2), (data))
#define	dwarf_data4(s,data) \
	write32le(section_ptr_add((s), 4), (data))
#define	dwarf_data8(s,data) \
	write64le(section_ptr_add((s), 8), (data))

static int dwarf_get_section_sym(Section *s)
{
    TCCState *s1 = s->s1;
    return put_elf_sym(symtab_section, 0, 0,
                       ELFW(ST_INFO)(STB_LOCAL, STT_SECTION), 0,
                       s->sh_num, NULL);
}

static void dwarf_reloc(Section *s, int sym, int rel)
{
    TCCState *s1 = s->s1;
    put_elf_reloca(symtab_section, s, s->data_offset, rel, sym, 0);
}

static void dwarf_string(Section *s, Section *dw, int sym, const char *str)
{
    TCCState *s1 = s->s1;
    int offset, len;
    char *ptr;

    len = strlen(str) + 1;
    offset = dw->data_offset;
    ptr = section_ptr_add(dw, len);
    memmove(ptr, str, len);
    put_elf_reloca(symtab_section, s, s->data_offset, R_DATA_32DW, sym,
                   PTR_SIZE == 4 ? 0 : offset);
    dwarf_data4(s, PTR_SIZE == 4 ? offset : 0);
}

static void dwarf_strp(Section *s, const char *str)
{
    TCCState *s1 = s->s1;
    dwarf_string(s, dwarf_str_section, dwarf_sym.str, str);
}

static void dwarf_line_strp(Section *s, const char *str)
{
    TCCState *s1 = s->s1;
    dwarf_string(s, dwarf_line_str_section, dwarf_sym.line_str, str);
}

static void dwarf_line_op(TCCState *s1, unsigned char op)
{
    if (dwarf_line.line_size >= dwarf_line.line_max_size) {
	dwarf_line.line_max_size += 1024;
	dwarf_line.line_data =
	    (unsigned char *)tcc_realloc(dwarf_line.line_data,
					 dwarf_line.line_max_size);
    }
    dwarf_line.line_data[dwarf_line.line_size++] = op;
}

static void dwarf_file(TCCState *s1)
{
    int i;
    char *filename;

    filename = strrchr(file->filename, '/');
    if (filename == NULL) {
        for (i = 0; i < dwarf_line.filename_size; i++)
            if (dwarf_line.filename_table[i].dir_entry == 0 &&
		strcmp(dwarf_line.filename_table[i].name,
		file->filename) == 0) {
		    dwarf_line.cur_file = i;
	            return;
		}
    }
    else {
	int j;
	char *undo = filename;

	*filename++ = '\0';
        for (i = 0; i < dwarf_line.dir_size; i++)
	    if (strcmp(dwarf_line.dir_table[i], file->filename) == 0)
		for (j = 1; j < dwarf_line.filename_size; j++)
		    if (dwarf_line.filename_table[j].dir_entry == i &&
			strcmp(dwarf_line.filename_table[j].name,
			filename) == 0) {
			*undo = '/';
		        dwarf_line.cur_file = j;
			return;
		    }
	*undo = '/';
    }
    return;
}

#if 0
static int dwarf_uleb128_size (unsigned long long value)
{
    int size =  0;

    do {
        value >>= 7;
        size++;
    } while (value != 0);
    return size;
}
#endif

static int dwarf_sleb128_size (long long value)
{
    int size =  0;
    long long end = value >> 63;
    unsigned char last = end & 0x40;
    unsigned char byte;

    do {
        byte = value & 0x7f;
        value >>= 7;
        size++;
    } while (value != end || (byte & 0x40) != last);
    return size;
}

static void dwarf_uleb128 (Section *s, unsigned long long value)
{
    do {
        unsigned char byte = value & 0x7f;

        value >>= 7;
        dwarf_data1(s, byte | (value ? 0x80 : 0));
    } while (value != 0);
}

static void dwarf_sleb128 (Section *s, long long value)
{
    int more;
    long long end = value >> 63;
    unsigned char last = end & 0x40;

    do {
        unsigned char byte = value & 0x7f;

        value >>= 7;
	more = value != end || (byte & 0x40) != last;
        dwarf_data1(s, byte | (0x80 * more));
    } while (more);
}

static void dwarf_uleb128_op (TCCState *s1, unsigned long long value)
{
    do {
        unsigned char byte = value & 0x7f;

        value >>= 7;
        dwarf_line_op(s1, byte | (value ? 0x80 : 0));
    } while (value != 0);
}

static void dwarf_sleb128_op (TCCState *s1, long long value)
{
    int more;
    long long end = value >> 63;
    unsigned char last = end & 0x40;

    do {
        unsigned char byte = value & 0x7f;

        value >>= 7;
        more = value != end || (byte & 0x40) != last;
        dwarf_line_op(s1, byte | (0x80 * more));
    } while (more);
}

/* start of translation unit info */
ST_FUNC void tcc_debug_start(TCCState *s1)
{
    int i;
    char buf[512];
    char *filename;

    /* an elf symbol of type STT_FILE must be put so that STB_LOCAL
       symbols can be safely used */
    filename = file->prev ? file->prev->filename : file->filename;
    put_elf_sym(symtab_section, 0, 0,
                ELFW(ST_INFO)(STB_LOCAL, STT_FILE), 0,
                SHN_ABS, filename);

    if (s1->do_debug) {

        new_file = last_line_num = 0;
        debug_next_type = N_DEFAULT_DEBUG;
        debug_hash = NULL;
        debug_anon_hash = NULL;
        n_debug_hash = 0;
        n_debug_anon_hash = 0;

        getcwd(buf, sizeof(buf));
#ifdef _WIN32
        normalize_slashes(buf);
#endif

        if (s1->dwarf) {
            int start_abbrev;
            unsigned char *ptr;
	    char *undo;

            /* dwarf_abbrev */
            start_abbrev = dwarf_abbrev_section->data_offset;
            ptr = section_ptr_add(dwarf_abbrev_section, sizeof(dwarf_abbrev_init));
            memcpy(ptr, dwarf_abbrev_init, sizeof(dwarf_abbrev_init));

            if (s1->dwarf < 5) {
    	        while (*ptr) {
    	            ptr += 3;
    	            while (*ptr) {
    	                if (ptr[1] == DW_FORM_line_strp)
    		            ptr[1] = DW_FORM_strp;
		        if (s1->dwarf < 4) {
			    /* These are compatable for DW_TAG_compile_unit
			       DW_AT_stmt_list. */
			    if  (ptr[1] == DW_FORM_sec_offset)
			         ptr[1] = DW_FORM_data4;
			    /* This code uses only size < 0x80 so these are
			       compatible. */
			    if  (ptr[1] == DW_FORM_exprloc)
			         ptr[1] = DW_FORM_block1;
			}
    	                ptr += 2;
    	            }
		    ptr += 2;
    	        }
            }

            dwarf_sym.info = dwarf_get_section_sym(dwarf_info_section);
            dwarf_sym.abbrev = dwarf_get_section_sym(dwarf_abbrev_section);
            dwarf_sym.line = dwarf_get_section_sym(dwarf_line_section);
            dwarf_sym.str = dwarf_get_section_sym(dwarf_str_section);
            if (tcc_state->dwarf >= 5)
    	        dwarf_sym.line_str = dwarf_get_section_sym(dwarf_line_str_section);
            else {
    	        dwarf_line_str_section = dwarf_str_section;
                dwarf_sym.line_str = dwarf_sym.str;
            }
            section_sym = dwarf_get_section_sym(text_section);

            /* dwarf_info */
            dwarf_info.start = dwarf_info_section->data_offset;
            dwarf_data4(dwarf_info_section, 0); // size
            dwarf_data2(dwarf_info_section, s1->dwarf); // version
            if (s1->dwarf >= 5) {
                dwarf_data1(dwarf_info_section, DW_UT_compile); // unit type
                dwarf_data1(dwarf_info_section, PTR_SIZE);
                dwarf_reloc(dwarf_info_section, dwarf_sym.abbrev, R_DATA_32DW);
                dwarf_data4(dwarf_info_section, start_abbrev);
            }
            else {
                dwarf_reloc(dwarf_info_section, dwarf_sym.abbrev, R_DATA_32DW);
                dwarf_data4(dwarf_info_section, start_abbrev);
                dwarf_data1(dwarf_info_section, PTR_SIZE);
            }

            dwarf_data1(dwarf_info_section, DWARF_ABBREV_COMPILE_UNIT);
            dwarf_strp(dwarf_info_section, "tcc " TCC_VERSION);
            dwarf_data1(dwarf_info_section, DW_LANG_C11);
            dwarf_line_strp(dwarf_info_section, filename);
            dwarf_line_strp(dwarf_info_section, buf);
            dwarf_reloc(dwarf_info_section, section_sym, R_DATA_PTR);
#if PTR_SIZE == 4
            dwarf_data4(dwarf_info_section, ind); // low pc
            dwarf_data4(dwarf_info_section, 0); // high pc
#else
            dwarf_data8(dwarf_info_section, ind); // low pc
            dwarf_data8(dwarf_info_section, 0); // high pc
#endif
            dwarf_reloc(dwarf_info_section, dwarf_sym.line, R_DATA_32DW);
            dwarf_data4(dwarf_info_section, dwarf_line_section->data_offset); // stmt_list

            /* dwarf_line */
            dwarf_line.start = dwarf_line_section->data_offset;
            dwarf_data4(dwarf_line_section, 0); // length
            dwarf_data2(dwarf_line_section, s1->dwarf); // version
            if (s1->dwarf >= 5) {
                dwarf_data1(dwarf_line_section, PTR_SIZE); // address size
                dwarf_data1(dwarf_line_section, 0); // segment selector
            }
            dwarf_data4(dwarf_line_section, 0); // prologue Length
            dwarf_data1(dwarf_line_section, DWARF_MIN_INSTR_LEN);
            if (s1->dwarf >= 4)
                dwarf_data1(dwarf_line_section, 1); // maximum ops per instruction
            dwarf_data1(dwarf_line_section, 1); // Initial value of 'is_stmt'
            dwarf_data1(dwarf_line_section, DWARF_LINE_BASE);
            dwarf_data1(dwarf_line_section, DWARF_LINE_RANGE);
            dwarf_data1(dwarf_line_section, DWARF_OPCODE_BASE);
            ptr = section_ptr_add(dwarf_line_section, sizeof(dwarf_line_opcodes));
            memcpy(ptr, dwarf_line_opcodes, sizeof(dwarf_line_opcodes));
	    undo = strrchr(filename, '/');
	    if (undo)
		*undo = 0;
            dwarf_line.dir_size = 1 + (undo != NULL);
            dwarf_line.dir_table = (char **) tcc_malloc(sizeof (char *) *
							dwarf_line.dir_size);
            dwarf_line.dir_table[0] = tcc_strdup(buf);
	    if (undo)
                dwarf_line.dir_table[1] = tcc_strdup(filename);
            dwarf_line.filename_size = 2;
            dwarf_line.filename_table =
    	        (struct dwarf_filename_struct *)
    	        tcc_malloc(2*sizeof (struct dwarf_filename_struct));
            dwarf_line.filename_table[0].dir_entry = 0;
	    if (undo) {
                dwarf_line.filename_table[0].name = tcc_strdup(filename);
                dwarf_line.filename_table[1].dir_entry = 1;
                dwarf_line.filename_table[1].name = tcc_strdup(undo + 1);
		*undo = '/';
                dwarf_line.filename_table[0].name = tcc_strdup(filename);
	    }
	    else {
                dwarf_line.filename_table[0].name = tcc_strdup(filename);
                dwarf_line.filename_table[1].dir_entry = 0;
                dwarf_line.filename_table[1].name = tcc_strdup(filename);
	    }
            dwarf_line.line_size = dwarf_line.line_max_size = 0;
            dwarf_line.line_data = NULL;
            dwarf_line.cur_file = 1;
            dwarf_line.last_file = 0;
            dwarf_line.last_pc = 0;
            dwarf_line.last_line = 1;
            dwarf_line_op(s1, 0); // extended
            dwarf_uleb128_op(s1, 1 + PTR_SIZE); // extended size
            dwarf_line_op(s1, DW_LNE_set_address);
            for (i = 0; i < PTR_SIZE; i++)
    	        dwarf_line_op(s1, 0);
            memset(&dwarf_info.base_type_used, 0, sizeof(dwarf_info.base_type_used));
        }
        else
        {
            /* file info: full path + filename */
            pstrcat(buf, sizeof(buf), "/");
            section_sym = put_elf_sym(symtab_section, 0, 0,
                                      ELFW(ST_INFO)(STB_LOCAL, STT_SECTION), 0,
                                      text_section->sh_num, NULL);
            put_stabs_r(s1, buf, N_SO, 0, 0,
                        text_section->data_offset, text_section, section_sym);
            put_stabs_r(s1, filename, N_SO, 0, 0,
                        text_section->data_offset, text_section, section_sym);
            for (i = 0; i < N_DEFAULT_DEBUG; i++)
                put_stabs(s1, default_debug[i].name, N_LSYM, 0, 0, 0);
        }
        /* we're currently 'including' the <command line> */
        tcc_debug_bincl(s1);
    }
}

/* put end of translation unit info */
ST_FUNC void tcc_debug_end(TCCState *s1)
{
    if (!s1->do_debug)
        return;
    if (s1->dwarf) {
	int i, j;
	int start_aranges;
	unsigned char *ptr;
	int text_size = text_section->data_offset;

	/* dwarf_info */
	for (i = 0; i < n_debug_anon_hash; i++) {
	    Sym *t = debug_anon_hash[i].type;
	    int pos = dwarf_info_section->data_offset;

	    dwarf_data1(dwarf_info_section,
                        IS_UNION (t->type.t) ? DWARF_ABBREV_UNION_TYPE
                                             : DWARF_ABBREV_STRUCTURE_TYPE);
            dwarf_strp(dwarf_info_section,
                       (t->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM
                       ? "" : get_tok_str(t->v & ~SYM_STRUCT, NULL));
            dwarf_uleb128(dwarf_info_section, 0);
            dwarf_uleb128(dwarf_info_section, dwarf_line.cur_file);
            dwarf_uleb128(dwarf_info_section, file->line_num);
	    j = dwarf_info_section->data_offset + 5 - dwarf_info.start;
	    dwarf_data4(dwarf_info_section, j);
	    dwarf_data1(dwarf_info_section, 0);
	    for (j = 0; j < debug_anon_hash[i].n_debug_type; j++)
		write32le(dwarf_info_section->data +
			  debug_anon_hash[i].debug_type[j],
			  pos - dwarf_info.start);
	    tcc_free (debug_anon_hash[i].debug_type);
	}
	tcc_free (debug_anon_hash);
	dwarf_data1(dwarf_info_section, 0);
	ptr = dwarf_info_section->data + dwarf_info.start;
	write32le(ptr, dwarf_info_section->data_offset - dwarf_info.start - 4);
	write32le(ptr + 25 + (s1->dwarf >= 5) + PTR_SIZE, text_size);

	/* dwarf_aranges */
	start_aranges = dwarf_aranges_section->data_offset;
	dwarf_data4(dwarf_aranges_section, 0); // size
	dwarf_data2(dwarf_aranges_section, 2); // version
	dwarf_reloc(dwarf_aranges_section, dwarf_sym.info, R_DATA_32DW);
	dwarf_data4(dwarf_aranges_section, 0); // dwarf_info
#if PTR_SIZE == 4
	dwarf_data1(dwarf_aranges_section, 4); // address size
#else
	dwarf_data1(dwarf_aranges_section, 8); // address size
#endif
	dwarf_data1(dwarf_aranges_section, 0); // segment selector size
	dwarf_data4(dwarf_aranges_section, 0); // padding
	dwarf_reloc(dwarf_aranges_section, section_sym, R_DATA_PTR);
#if PTR_SIZE == 4
	dwarf_data4(dwarf_aranges_section, 0); // Begin
	dwarf_data4(dwarf_aranges_section, text_size); // End
	dwarf_data4(dwarf_aranges_section, 0); // End list
	dwarf_data4(dwarf_aranges_section, 0); // End list
#else
	dwarf_data8(dwarf_aranges_section, 0); // Begin
	dwarf_data8(dwarf_aranges_section, text_size); // End
	dwarf_data8(dwarf_aranges_section, 0); // End list
	dwarf_data8(dwarf_aranges_section, 0); // End list
#endif
	ptr = dwarf_aranges_section->data + start_aranges;
	write32le(ptr, dwarf_aranges_section->data_offset - start_aranges - 4);

	/* dwarf_line */
	if (s1->dwarf >= 5) {
	    dwarf_data1(dwarf_line_section, 1); /* col */
	    dwarf_uleb128(dwarf_line_section, DW_LNCT_path);
	    dwarf_uleb128(dwarf_line_section, DW_FORM_line_strp);
	    dwarf_uleb128(dwarf_line_section, dwarf_line.dir_size);
	    for (i = 0; i < dwarf_line.dir_size; i++)
	        dwarf_line_strp(dwarf_line_section, dwarf_line.dir_table[i]);
	    dwarf_data1(dwarf_line_section, 2); /* col */
	    dwarf_uleb128(dwarf_line_section, DW_LNCT_path);
	    dwarf_uleb128(dwarf_line_section, DW_FORM_line_strp);
	    dwarf_uleb128(dwarf_line_section, DW_LNCT_directory_index);
	    dwarf_uleb128(dwarf_line_section, DW_FORM_udata);
	    dwarf_uleb128(dwarf_line_section, dwarf_line.filename_size);
	    for (i = 0; i < dwarf_line.filename_size; i++) {
	        dwarf_line_strp(dwarf_line_section,
				dwarf_line.filename_table[i].name);
	        dwarf_uleb128(dwarf_line_section,
			      dwarf_line.filename_table[i].dir_entry);
	    }
	}
	else {
	    int len;

	    for (i = 0; i < dwarf_line.dir_size; i++) {
	        len = strlen(dwarf_line.dir_table[i]) + 1;
	        ptr = section_ptr_add(dwarf_line_section, len);
	        memmove(ptr, dwarf_line.dir_table[i], len);
	    }
	    dwarf_data1(dwarf_line_section, 0); /* end dir */
	    for (i = 0; i < dwarf_line.filename_size; i++) {
	        len = strlen(dwarf_line.filename_table[i].name) + 1;
	        ptr = section_ptr_add(dwarf_line_section, len);
	        memmove(ptr, dwarf_line.filename_table[i].name, len);
	        dwarf_uleb128(dwarf_line_section,
			      dwarf_line.filename_table[i].dir_entry);
	        dwarf_uleb128(dwarf_line_section, 0); /* time */
	        dwarf_uleb128(dwarf_line_section, 0); /* size */
	    }
	    dwarf_data1(dwarf_line_section, 0); /* end file */
	}
	for (i = 0; i < dwarf_line.dir_size; i++)
	    tcc_free(dwarf_line.dir_table[i]);
	tcc_free(dwarf_line.dir_table);
	for (i = 0; i < dwarf_line.filename_size; i++)
	    tcc_free(dwarf_line.filename_table[i].name);
	tcc_free(dwarf_line.filename_table);

	dwarf_line_op(s1, 0); // extended
	dwarf_uleb128_op(s1, 1); // extended size
	dwarf_line_op(s1, DW_LNE_end_sequence);
	i = (s1->dwarf >= 5) * 2;
	write32le(&dwarf_line_section->data[dwarf_line.start + 6 + i],
		  dwarf_line_section->data_offset - dwarf_line.start - (10 + i));
	section_ptr_add(dwarf_line_section, 3);
	dwarf_reloc(dwarf_line_section, section_sym, R_DATA_PTR);
	ptr = section_ptr_add(dwarf_line_section, dwarf_line.line_size - 3);
	memmove(ptr - 3, dwarf_line.line_data, dwarf_line.line_size);
	tcc_free(dwarf_line.line_data);
	write32le(dwarf_line_section->data + dwarf_line.start,
		  dwarf_line_section->data_offset - dwarf_line.start - 4);
    }
    else
    {
        put_stabs_r(s1, NULL, N_SO, 0, 0,
                    text_section->data_offset, text_section, section_sym);
    }
    tcc_free(debug_hash);
}

static BufferedFile* put_new_file(TCCState *s1)
{
    BufferedFile *f = file;
    /* use upper file if from inline ":asm:" */
    if (f->filename[0] == ':')
        f = f->prev;
    if (f && new_file) {
        new_file = last_line_num = 0;
        if (s1->dwarf)
            dwarf_file(s1);
        else
            put_stabs_r(s1, f->filename, N_SOL, 0, 0, ind, text_section, section_sym);
    }
    return f;
}

/* put alternative filename */
ST_FUNC void tcc_debug_putfile(TCCState *s1, const char *filename)
{
    if (0 == strcmp(file->filename, filename))
        return;
    pstrcpy(file->filename, sizeof(file->filename), filename);
    if (!s1->do_debug)
        return;
    if (s1->dwarf)
        dwarf_file(s1);
    new_file = 1;
}

/* begin of #include */
ST_FUNC void tcc_debug_bincl(TCCState *s1)
{
    if (!s1->do_debug)
        return;
    if (s1->dwarf) {
	int i, j;
	char *filename = strrchr(file->filename, '/');
	char *dir;

	if (filename == NULL) {
	    filename = file->filename;
	    i = 0;
	}
	else {
	    char *undo = filename;

	    *filename++ = '\0';
	    dir = file->filename;
	    for (i = 0; i < dwarf_line.dir_size; i++)
	        if (strcmp (dwarf_line.dir_table[i], dir) == 0)
		    break;
	    if (i == dwarf_line.dir_size) {
	        dwarf_line.dir_size++;
	        dwarf_line.dir_table =
		    (char **) tcc_realloc(dwarf_line.dir_table,
					  dwarf_line.dir_size *
					  sizeof (char *));
	        dwarf_line.dir_table[i] = tcc_strdup(dir);
	    }
	    *undo = '/';
	}
        if (strcmp(filename, "<command line>")) {
	    for (j = 0; j < dwarf_line.filename_size; j++)
	        if (dwarf_line.filename_table[j].dir_entry == i &&
		    strcmp (dwarf_line.filename_table[j].name, filename) == 0)
		    break;
	    if (j == dwarf_line.filename_size) {
	        dwarf_line.filename_table =
		    (struct dwarf_filename_struct *)
		    tcc_realloc(dwarf_line.filename_table,
			        (dwarf_line.filename_size + 1) *
			        sizeof (struct dwarf_filename_struct));
	        dwarf_line.filename_table[dwarf_line.filename_size].dir_entry = i;
	        dwarf_line.filename_table[dwarf_line.filename_size++].name =
		    tcc_strdup(filename);
	    }
        }
        dwarf_file(s1);
    }
    else
    {
        put_stabs(s1, file->filename, N_BINCL, 0, 0, 0);
    }
    new_file = 1;
}

/* end of #include */
ST_FUNC void tcc_debug_eincl(TCCState *s1)
{
    if (!s1->do_debug)
        return;
    if (s1->dwarf)
        dwarf_file(s1);
    else
        put_stabn(s1, N_EINCL, 0, 0, 0);
    new_file = 1;
}

/* generate line number info */
ST_FUNC void tcc_debug_line(TCCState *s1)
{
    BufferedFile *f;

    if (!s1->do_debug)
        return;
    if (cur_text_section != text_section)
        return;
    f = put_new_file(s1);
    if (!f)
        return;
    if (last_line_num == f->line_num)
        return;
    last_line_num = f->line_num;

    if (s1->dwarf) {
	int len_pc = (ind - dwarf_line.last_pc) / DWARF_MIN_INSTR_LEN;
	int len_line = f->line_num - dwarf_line.last_line;
	int n = len_pc * DWARF_LINE_RANGE + len_line + DWARF_OPCODE_BASE - DWARF_LINE_BASE;

	if (dwarf_line.cur_file != dwarf_line.last_file) {
	    dwarf_line.last_file = dwarf_line.cur_file;
	    dwarf_line_op(s1, DW_LNS_set_file);
	    dwarf_uleb128_op(s1, dwarf_line.cur_file);
	}
	if (len_pc &&
	    len_line >= DWARF_LINE_BASE && len_line <= (DWARF_OPCODE_BASE + DWARF_LINE_BASE) &&
	    n >= DWARF_OPCODE_BASE && n <= 255)
            dwarf_line_op(s1, n);
	else {
	    if (len_pc) {
	        n = len_pc * DWARF_LINE_RANGE + 0 + DWARF_OPCODE_BASE - DWARF_LINE_BASE;
	        if (n >= DWARF_OPCODE_BASE && n <= 255)
                    dwarf_line_op(s1, n);
		else {
	            dwarf_line_op(s1, DW_LNS_advance_pc);
		    dwarf_uleb128_op(s1, len_pc);
		}
	    }
	    if (len_line) {
	        n = 0 * DWARF_LINE_RANGE + len_line + DWARF_OPCODE_BASE - DWARF_LINE_BASE;
	        if (len_line >= DWARF_LINE_BASE && len_line <= (DWARF_OPCODE_BASE + DWARF_LINE_BASE) &&
		    n >= DWARF_OPCODE_BASE && n <= 255)
	            dwarf_line_op(s1, n);
		else {
	            dwarf_line_op(s1, DW_LNS_advance_line);
		    dwarf_sleb128_op(s1, len_line);
		}
	    }
	}
	dwarf_line.last_pc = ind;
	dwarf_line.last_line = f->line_num;
    }
    else
    {
	if (func_ind != -1) {
            put_stabn(s1, N_SLINE, 0, f->line_num, ind - func_ind);
        } else {
            /* from tcc_assemble */
            put_stabs_r(s1, NULL, N_SLINE, 0, f->line_num, ind, text_section, section_sym);
        }
    }
}

static void tcc_debug_stabs (TCCState *s1, const char *str, int type, unsigned long value,
                             Section *sec, int sym_index, int info)
{
    struct debug_sym *s;

    if (debug_info) {
        debug_info->sym =
            (struct debug_sym *)tcc_realloc (debug_info->sym,
                                             sizeof(struct debug_sym) *
                                             (debug_info->n_sym + 1));
        s = debug_info->sym + debug_info->n_sym++;
        s->type = type;
        s->value = value;
        s->str = tcc_strdup(str);
        s->sec = sec;
        s->sym_index = sym_index;
        s->info = info;
        s->file = dwarf_line.cur_file;
        s->line = file->line_num;
    }
    else if (sec)
        put_stabs_r (s1, str, type, 0, 0, value, sec, sym_index);
    else
        put_stabs (s1, str, type, 0, 0, value);
}

ST_FUNC void tcc_debug_stabn(TCCState *s1, int type, int value)
{
    if (!s1->do_debug)
        return;
    if (type == N_LBRAC) {
        struct _debug_info *info =
            (struct _debug_info *) tcc_mallocz(sizeof (*info));

        info->start = value;
        info->parent = debug_info;
        if (debug_info) {
            if (debug_info->child) {
                if (debug_info->child->last)
                    debug_info->child->last->next = info;
                else
                    debug_info->child->next = info;
                debug_info->child->last = info;
            }
            else
                debug_info->child = info;
        }
        else
            debug_info_root = info;
        debug_info = info;
    }
    else {
        debug_info->end = value;
        debug_info = debug_info->parent;
    }
}

static int tcc_debug_find(TCCState *s1, Sym *t, int dwarf)
{
    int i;

    if (!debug_info && dwarf &&
	(t->type.t & VT_BTYPE) == VT_STRUCT && t->c == -1) {
	for (i = 0; i < n_debug_anon_hash; i++)
            if (t == debug_anon_hash[i].type)
		return 0;
	debug_anon_hash = (struct _debug_anon_hash *)
            tcc_realloc (debug_anon_hash,
                         (n_debug_anon_hash + 1) * sizeof(*debug_anon_hash));
        debug_anon_hash[n_debug_anon_hash].n_debug_type = 0;
        debug_anon_hash[n_debug_anon_hash].debug_type = NULL;
        debug_anon_hash[n_debug_anon_hash++].type = t;
	return 0;
    }
    for (i = 0; i < n_debug_hash; i++)
        if (t == debug_hash[i].type)
	    return debug_hash[i].debug_type;
    return -1;
}

static int tcc_get_dwarf_info(TCCState *s1, Sym *s);

static void tcc_debug_check_anon(TCCState *s1, Sym *t, int debug_type)
{
    int i;

    if (!debug_info && (t->type.t & VT_BTYPE) == VT_STRUCT && t->type.ref->c == -1)
	for (i = 0; i < n_debug_anon_hash; i++)
            if (t->type.ref == debug_anon_hash[i].type) {
		debug_anon_hash[i].debug_type =
		    tcc_realloc(debug_anon_hash[i].debug_type,
				(debug_anon_hash[i].n_debug_type + 1) * sizeof(int));
		debug_anon_hash[i].debug_type[debug_anon_hash[i].n_debug_type++] =
		    debug_type;
            }
}

ST_FUNC void tcc_debug_fix_anon(TCCState *s1, CType *t)
{
    int i, j, debug_type;

    if (!s1->do_debug || !s1->dwarf || debug_info)
	return;
    if ((t->t & VT_BTYPE) == VT_STRUCT && t->ref->c != -1)
	for (i = 0; i < n_debug_anon_hash; i++)
	    if (t->ref == debug_anon_hash[i].type) {
		Sym sym = {0}; sym .type = *t ;

		/* Trick to not hash this struct */
		debug_info = (struct _debug_info *) t;
		debug_type = tcc_get_dwarf_info(s1, &sym);
		debug_info = NULL;
		for (j = 0; j < debug_anon_hash[i].n_debug_type; j++)
		    write32le(dwarf_info_section->data +
			      debug_anon_hash[i].debug_type[j],
			      debug_type - dwarf_info.start);
		tcc_free(debug_anon_hash[i].debug_type);
		n_debug_anon_hash--;
		for (; i < n_debug_anon_hash; i++)
		    debug_anon_hash[i] = debug_anon_hash[i + 1];
	    }
}

static int tcc_debug_add(TCCState *s1, Sym *t, int dwarf)
{
    int offset = dwarf ? dwarf_info_section->data_offset : ++debug_next_type;
    debug_hash = (struct _debug_hash *)
	tcc_realloc (debug_hash,
		     (n_debug_hash + 1) * sizeof(*debug_hash));
    debug_hash[n_debug_hash].debug_type = offset;
    debug_hash[n_debug_hash++].type = t;
    return offset;
}

static void tcc_debug_remove(TCCState *s1, Sym *t)
{
    int i;

    for (i = 0; i < n_debug_hash; i++)
        if (t == debug_hash[i].type) {
	    n_debug_hash--;
	    for (; i < n_debug_hash; i++)
		debug_hash[i] = debug_hash[i+1];
	}
}

static void tcc_get_debug_info(TCCState *s1, Sym *s, CString *result)
{
    int type;
    int n = 0;
    int debug_type = -1;
    Sym *t = s;
    CString str;

    for (;;) {
        type = t->type.t & ~(VT_STORAGE | VT_CONSTANT | VT_VOLATILE | VT_VLA);
        if ((type & VT_BTYPE) != VT_BYTE)
            type &= ~VT_DEFSIGN;
        if (type == VT_PTR || type == (VT_PTR | VT_ARRAY))
            n++, t = t->type.ref;
        else
            break;
    }
    if ((type & VT_BTYPE) == VT_STRUCT) {
	Sym *e = t;

        t = t->type.ref;
	debug_type = tcc_debug_find(s1, t, 0);
        if (debug_type == -1) {
            debug_type = tcc_debug_add(s1, t, 0);
            cstr_new (&str);
            cstr_printf (&str, "%s:T%d=%c%d",
                         (t->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM
                         ? "" : get_tok_str(t->v & ~SYM_STRUCT, NULL),
                         debug_type,
                         IS_UNION (t->type.t) ? 'u' : 's',
                         t->c);
            while (t->next) {
                int pos, size, align;

                t = t->next;
                cstr_printf (&str, "%s:",
                             (t->v & ~SYM_FIELD) >= SYM_FIRST_ANOM
                             ? "" : get_tok_str(t->v & ~SYM_FIELD, NULL));
                tcc_get_debug_info (s1, t, &str);
                if (t->type.t & VT_BITFIELD) {
                    pos = t->c * 8 + BIT_POS(t->type.t);
                    size = BIT_SIZE(t->type.t);
                }
                else {
                    pos = t->c * 8;
                    size = type_size(&t->type, &align) * 8;
                }
                cstr_printf (&str, ",%d,%d;", pos, size);
            }
            cstr_printf (&str, ";");
            tcc_debug_stabs(s1, str.data, N_LSYM, 0, NULL, 0, 0);
            cstr_free (&str);
            if (debug_info)
                tcc_debug_remove(s1, e);
        }
    }
    else if (IS_ENUM(type)) {
        Sym *e = t = t->type.ref;

	debug_type = tcc_debug_find(s1, t, 0);
	if (debug_type == -1) {
	    debug_type = tcc_debug_add(s1, t, 0);
            cstr_new (&str);
            cstr_printf (&str, "%s:T%d=e",
                         (t->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM
                         ? "" : get_tok_str(t->v & ~SYM_STRUCT, NULL),
                         debug_type);
            while (t->next) {
                t = t->next;
                cstr_printf (&str, "%s:",
                             (t->v & ~SYM_FIELD) >= SYM_FIRST_ANOM
                             ? "" : get_tok_str(t->v & ~SYM_FIELD, NULL));
                cstr_printf (&str, e->type.t & VT_UNSIGNED ? "%u," : "%d,",
                             (int)t->enum_val);
            }
            cstr_printf (&str, ";");
            tcc_debug_stabs(s1, str.data, N_LSYM, 0, NULL, 0, 0);
            cstr_free (&str);
            if (debug_info)
                tcc_debug_remove(s1, e);
	}
    }
    else if ((type & VT_BTYPE) != VT_FUNC) {
        type &= ~VT_STRUCT_MASK;
        for (debug_type = 1; debug_type <= N_DEFAULT_DEBUG; debug_type++)
            if (default_debug[debug_type - 1].type == type)
                break;
        if (debug_type > N_DEFAULT_DEBUG)
            return;
    }
    if (n > 0)
        cstr_printf (result, "%d=", ++debug_next_type);
    t = s;
    for (;;) {
        type = t->type.t & ~(VT_STORAGE | VT_CONSTANT | VT_VOLATILE | VT_VLA);
        if ((type & VT_BTYPE) != VT_BYTE)
            type &= ~VT_DEFSIGN;
        if (type == VT_PTR)
            cstr_printf (result, "%d=*", ++debug_next_type);
        else if (type == (VT_PTR | VT_ARRAY))
            cstr_printf (result, "%d=ar1;0;%d;",
                         ++debug_next_type, t->type.ref->c - 1);
        else if (type == VT_FUNC) {
            cstr_printf (result, "%d=f", ++debug_next_type);
            tcc_get_debug_info (s1, t->type.ref, result);
            return;
        }
        else
            break;
        t = t->type.ref;
    }
    cstr_printf (result, "%d", debug_type);
}

static int tcc_get_dwarf_info(TCCState *s1, Sym *s)
{
    int type;
    int debug_type = -1;
    Sym *e, *t = s;
    int i;
    int last_pos = -1;
    int retval;

    if (new_file)
        put_new_file(s1);
    for (;;) {
        type = t->type.t & ~(VT_STORAGE | VT_CONSTANT | VT_VOLATILE | VT_VLA);
        if ((type & VT_BTYPE) != VT_BYTE)
            type &= ~VT_DEFSIGN;
        if (type == VT_PTR || type == (VT_PTR | VT_ARRAY))
            t = t->type.ref;
        else
            break;
    }
    if ((type & VT_BTYPE) == VT_STRUCT) {
        t = t->type.ref;
	debug_type = tcc_debug_find(s1, t, 1);
	if (debug_type == -1) {
	    int pos_sib, i, *pos_type;

	    debug_type = tcc_debug_add(s1, t, 1);
	    e = t;
	    i = 0;
	    while (e->next) {
		e = e->next;
		i++;
	    }
	    pos_type = (int *) tcc_malloc(i * sizeof(int));
	    dwarf_data1(dwarf_info_section,
			IS_UNION (t->type.t) ? DWARF_ABBREV_UNION_TYPE
					     : DWARF_ABBREV_STRUCTURE_TYPE);
	    dwarf_strp(dwarf_info_section,
                       (t->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM
                       ? "" : get_tok_str(t->v & ~SYM_STRUCT, NULL));
	    dwarf_uleb128(dwarf_info_section, t->c);
	    dwarf_uleb128(dwarf_info_section, dwarf_line.cur_file);
	    dwarf_uleb128(dwarf_info_section, file->line_num);
	    pos_sib = dwarf_info_section->data_offset;
	    dwarf_data4(dwarf_info_section, 0);
	    e = t;
	    i = 0;
            while (e->next) {
                e = e->next;
	        dwarf_data1(dwarf_info_section,
			    e->type.t & VT_BITFIELD ? DWARF_ABBREV_MEMBER_BF
						    : DWARF_ABBREV_MEMBER);
		dwarf_strp(dwarf_info_section,
			   (e->v & ~SYM_FIELD) >= SYM_FIRST_ANOM
			   ? "" : get_tok_str(e->v & ~SYM_FIELD, NULL));
		dwarf_uleb128(dwarf_info_section, dwarf_line.cur_file);
		dwarf_uleb128(dwarf_info_section, file->line_num);
		pos_type[i++] = dwarf_info_section->data_offset;
		dwarf_data4(dwarf_info_section, 0);
                if (e->type.t & VT_BITFIELD) {
                    int pos = e->c * 8 + BIT_POS(e->type.t);
                    int size = BIT_SIZE(e->type.t);

		    dwarf_uleb128(dwarf_info_section, size);
		    dwarf_uleb128(dwarf_info_section, pos);
		}
		else
		    dwarf_uleb128(dwarf_info_section, e->c);
	    }
	    dwarf_data1(dwarf_info_section, 0);
	    write32le(dwarf_info_section->data + pos_sib,
		      dwarf_info_section->data_offset - dwarf_info.start);
	    e = t;
	    i = 0;
	    while (e->next) {
		e = e->next;
		type = tcc_get_dwarf_info(s1, e);
		tcc_debug_check_anon(s1, e, pos_type[i]);
		write32le(dwarf_info_section->data + pos_type[i++],
			  type - dwarf_info.start);
	    }
	    tcc_free(pos_type);
	    if (debug_info)
		tcc_debug_remove(s1, t);
        }
    }
    else if (IS_ENUM(type)) {
        t = t->type.ref;
	debug_type = tcc_debug_find(s1, t, 1);
	if (debug_type == -1) {
	    int pos_sib, pos_type;
	    Sym sym = {0}; sym.type.t = VT_INT | (type & VT_UNSIGNED);

	    pos_type = tcc_get_dwarf_info(s1, &sym);
	    debug_type = tcc_debug_add(s1, t, 1);
	    dwarf_data1(dwarf_info_section, DWARF_ABBREV_ENUMERATION_TYPE);
	    dwarf_strp(dwarf_info_section,
                       (t->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM
                       ? "" : get_tok_str(t->v & ~SYM_STRUCT, NULL));
	    dwarf_data1(dwarf_info_section,
		        type & VT_UNSIGNED ? DW_ATE_unsigned : DW_ATE_signed );
	    dwarf_data1(dwarf_info_section, 4);
	    dwarf_data4(dwarf_info_section, pos_type - dwarf_info.start);
	    dwarf_uleb128(dwarf_info_section, dwarf_line.cur_file);
	    dwarf_uleb128(dwarf_info_section, file->line_num);
	    pos_sib = dwarf_info_section->data_offset;
	    dwarf_data4(dwarf_info_section, 0);
	    e = t;
            while (e->next) {
                e = e->next;
	        dwarf_data1(dwarf_info_section,
			type & VT_UNSIGNED ? DWARF_ABBREV_ENUMERATOR_UNSIGNED
					   : DWARF_ABBREV_ENUMERATOR_SIGNED);
	        dwarf_strp(dwarf_info_section,
                           (e->v & ~SYM_FIELD) >= SYM_FIRST_ANOM
                           ? "" : get_tok_str(e->v & ~SYM_FIELD, NULL));
		if (type & VT_UNSIGNED)
	            dwarf_uleb128(dwarf_info_section, e->enum_val);
		else
	            dwarf_sleb128(dwarf_info_section, e->enum_val);
            }
	    dwarf_data1(dwarf_info_section, 0);
	    write32le(dwarf_info_section->data + pos_sib,
		      dwarf_info_section->data_offset - dwarf_info.start);
	    if (debug_info)
		tcc_debug_remove(s1, t);
	}
    }
    else if ((type & VT_BTYPE) != VT_FUNC) {
        type &= ~VT_STRUCT_MASK;
        for (i = 1; i <= N_DEFAULT_DEBUG; i++)
            if (default_debug[i - 1].type == type)
                break;
        if (i > N_DEFAULT_DEBUG)
            return 0;
	debug_type = dwarf_info.base_type_used[i - 1];
	if (debug_type == 0) {
	    char name[100];

	    debug_type = dwarf_info_section->data_offset;
	    dwarf_data1(dwarf_info_section, DWARF_ABBREV_BASE_TYPE);
	    dwarf_uleb128(dwarf_info_section, default_debug[i - 1].size);
	    dwarf_data1(dwarf_info_section, default_debug[i - 1].encoding);
	    strncpy(name, default_debug[i - 1].name, sizeof(name) -1);
	    *strchr(name, ':') = 0;
	    dwarf_strp(dwarf_info_section, name);
	    dwarf_info.base_type_used[i - 1] = debug_type;
	}
    }
    retval = debug_type;
    e = NULL;
    t = s;
    for (;;) {
        type = t->type.t & ~(VT_STORAGE | VT_CONSTANT | VT_VOLATILE | VT_VLA);
        if ((type & VT_BTYPE) != VT_BYTE)
            type &= ~VT_DEFSIGN;
        if (type == VT_PTR) {
	    i = dwarf_info_section->data_offset;
	    if (retval == debug_type)
		retval = i;
	    dwarf_data1(dwarf_info_section, DWARF_ABBREV_POINTER);
	    dwarf_data1(dwarf_info_section, PTR_SIZE);
	    if (last_pos != -1) {
		tcc_debug_check_anon(s1, e, last_pos);
		write32le(dwarf_info_section->data + last_pos,
			  i - dwarf_info.start);
	    }
	    last_pos = dwarf_info_section->data_offset;
	    e = t->type.ref;
	    dwarf_data4(dwarf_info_section, 0);
	}
        else if (type == (VT_PTR | VT_ARRAY)) {
	    int sib_pos, sub_type;
#if LONG_SIZE == 4
	    Sym sym = {0}; sym.type.t = VT_LONG | VT_INT | VT_UNSIGNED;
#else
	    Sym sym = {0}; sym.type.t = VT_LLONG | VT_LONG | VT_UNSIGNED;
#endif

	    sub_type = tcc_get_dwarf_info(s1, &sym);
	    i = dwarf_info_section->data_offset;
	    if (retval == debug_type)
		retval = i;
	    dwarf_data1(dwarf_info_section, DWARF_ABBREV_ARRAY_TYPE);
	    if (last_pos != -1) {
		tcc_debug_check_anon(s1, e, last_pos);
		write32le(dwarf_info_section->data + last_pos,
			  i - dwarf_info.start);
	    }
	    last_pos = dwarf_info_section->data_offset;
	    e = t->type.ref;
	    dwarf_data4(dwarf_info_section, 0);
	    sib_pos = dwarf_info_section->data_offset;
	    dwarf_data4(dwarf_info_section, 0);
	    for (;;) {
	        dwarf_data1(dwarf_info_section, DWARF_ABBREV_SUBRANGE_TYPE);
	        dwarf_data4(dwarf_info_section, sub_type - dwarf_info.start);
	        dwarf_uleb128(dwarf_info_section, t->type.ref->c - 1);
		s = t->type.ref;
		type = s->type.t & ~(VT_STORAGE | VT_CONSTANT | VT_VOLATILE);
		if (type != (VT_PTR | VT_ARRAY))
		    break;
		t = s;
	    }
	    dwarf_data1(dwarf_info_section, 0);
	    write32le(dwarf_info_section->data + sib_pos,
		      dwarf_info_section->data_offset - dwarf_info.start);
	}
        else if (type == VT_FUNC) {
	    int sib_pos, *pos_type;
	    Sym *f;

	    i = dwarf_info_section->data_offset;
	    debug_type = tcc_get_dwarf_info(s1, t->type.ref);
	    if (retval == debug_type)
		retval = i;
	    dwarf_data1(dwarf_info_section, DWARF_ABBREV_SUBROUTINE_TYPE);
	    if (last_pos != -1) {
		tcc_debug_check_anon(s1, e, last_pos);
		write32le(dwarf_info_section->data + last_pos,
			  i - dwarf_info.start);
	    }
	    last_pos = dwarf_info_section->data_offset;
	    e = t->type.ref;
	    dwarf_data4(dwarf_info_section, 0);
	    sib_pos = dwarf_info_section->data_offset;
	    dwarf_data4(dwarf_info_section, 0);
	    f = t->type.ref;
	    i = 0;
	    while (f->next) {
		f = f->next;
		i++;
	    }
	    pos_type = (int *) tcc_malloc(i * sizeof(int));
	    f = t->type.ref;
	    i = 0;
	    while (f->next) {
		f = f->next;
	        dwarf_data1(dwarf_info_section, DWARF_ABBREV_FORMAL_PARAMETER2);
		pos_type[i++] = dwarf_info_section->data_offset;
	        dwarf_data4(dwarf_info_section, 0);
	    }
	    dwarf_data1(dwarf_info_section, 0);
	    write32le(dwarf_info_section->data + sib_pos,
		      dwarf_info_section->data_offset - dwarf_info.start);
	    f = t->type.ref;
	    i = 0;
	    while (f->next) {
		f = f->next;
		type = tcc_get_dwarf_info(s1, f);
		tcc_debug_check_anon(s1, f, pos_type[i]);
	        write32le(dwarf_info_section->data + pos_type[i++],
                          type - dwarf_info.start);
	    }
	    tcc_free(pos_type);
        }
        else {
	    if (last_pos != -1) {
		tcc_debug_check_anon(s1, e, last_pos);
		write32le(dwarf_info_section->data + last_pos,
			  debug_type - dwarf_info.start);
	    }
            break;
	}
        t = t->type.ref;
    }
    return retval;
}

static void tcc_debug_finish (TCCState *s1, struct _debug_info *cur)
{
    while (cur) {
        struct _debug_info *next = cur->next;
        int i;

        if (s1->dwarf) {

            for (i = cur->n_sym - 1; i >= 0; i--) {
                struct debug_sym *s = &cur->sym[i];

		dwarf_data1(dwarf_info_section,
                            s->type == N_PSYM
			    ? DWARF_ABBREV_FORMAL_PARAMETER
			    : s->type == N_GSYM
                            ? DWARF_ABBREV_VARIABLE_EXTERNAL
                            : s->type == N_STSYM
			    ? DWARF_ABBREV_VARIABLE_STATIC
			    : DWARF_ABBREV_VARIABLE_LOCAL);
                dwarf_strp(dwarf_info_section, s->str);
		if (s->type == N_GSYM || s->type == N_STSYM) {
                    dwarf_uleb128(dwarf_info_section, s->file);
                    dwarf_uleb128(dwarf_info_section, s->line);
		}
                dwarf_data4(dwarf_info_section, s->info - dwarf_info.start);
		if (s->type == N_GSYM || s->type == N_STSYM) {
		    /* global/static */
		    if (s->type == N_GSYM)
                        dwarf_data1(dwarf_info_section, 1);
                    dwarf_data1(dwarf_info_section, PTR_SIZE + 1);
                    dwarf_data1(dwarf_info_section, DW_OP_addr);
		    if (s->type == N_STSYM)
		        dwarf_reloc(dwarf_info_section, section_sym, R_DATA_PTR);
#if PTR_SIZE == 4
                    dwarf_data4(dwarf_info_section, s->value);
#else
                    dwarf_data8(dwarf_info_section, s->value);
#endif
		}
		else {
		    /* param/local */
                    dwarf_data1(dwarf_info_section, dwarf_sleb128_size(s->value) + 1);
                    dwarf_data1(dwarf_info_section, DW_OP_fbreg);
                    dwarf_sleb128(dwarf_info_section, s->value);
		}
		tcc_free (s->str);
            }
            tcc_free (cur->sym);
            dwarf_data1(dwarf_info_section, DWARF_ABBREV_LEXICAL_BLOCK);
            dwarf_reloc(dwarf_info_section, section_sym, R_DATA_PTR);
#if PTR_SIZE == 4
            dwarf_data4(dwarf_info_section, func_ind + cur->start);
            dwarf_data4(dwarf_info_section, cur->end - cur->start);
#else
            dwarf_data8(dwarf_info_section, func_ind + cur->start);
            dwarf_data8(dwarf_info_section, cur->end - cur->start);
#endif
            tcc_debug_finish (s1, cur->child);
            dwarf_data1(dwarf_info_section, 0);
        }
        else
        {
            for (i = 0; i < cur->n_sym; i++) {
                struct debug_sym *s = &cur->sym[i];

                if (s->sec)
                    put_stabs_r(s1, s->str, s->type, 0, 0, s->value,
                                s->sec, s->sym_index);
                else
                    put_stabs(s1, s->str, s->type, 0, 0, s->value);
                tcc_free (s->str);
            }
            tcc_free (cur->sym);
            put_stabn(s1, N_LBRAC, 0, 0, cur->start);
            tcc_debug_finish (s1, cur->child);
            put_stabn(s1, N_RBRAC, 0, 0, cur->end);
        }
        tcc_free (cur);
        cur = next;
    }
}

ST_FUNC void tcc_add_debug_info(TCCState *s1, int param, Sym *s, Sym *e)
{
    CString debug_str;
    if (!s1->do_debug)
        return;
    cstr_new (&debug_str);
    for (; s != e; s = s->prev) {
        if (!s->v || (s->r & VT_VALMASK) != VT_LOCAL)
            continue;
	if (s1->dwarf) {
	    tcc_debug_stabs(s1, get_tok_str(s->v, NULL),
			    param ? N_PSYM : N_LSYM, s->c, NULL, 0,
			    tcc_get_dwarf_info(s1, s));
	}
	else
        {
            cstr_reset (&debug_str);
            cstr_printf (&debug_str, "%s:%s", get_tok_str(s->v, NULL),
			 param ? "p" : "");
            tcc_get_debug_info(s1, s, &debug_str);
            tcc_debug_stabs(s1, debug_str.data, param ? N_PSYM : N_LSYM,
			    s->c, NULL, 0, 0);
	}
    }
    cstr_free (&debug_str);
}

/* put function symbol */
ST_FUNC void tcc_debug_funcstart(TCCState *s1, Sym *sym)
{
    CString debug_str;
    BufferedFile *f;

    if (!s1->do_debug)
        return;
    debug_info_root = NULL;
    debug_info = NULL;
    tcc_debug_stabn(s1, N_LBRAC, ind - func_ind);
    f = put_new_file(s1);
    if (!f)
	return;

    if (s1->dwarf) {
        tcc_debug_line(s1);
	dwarf_line_op(s1, DW_LNS_copy);
        dwarf_info.func = sym;
        dwarf_info.line = file->line_num;
	if (s1->do_backtrace) {
	    int i, len;

	    dwarf_line_op(s1, 0); // extended
	    dwarf_uleb128_op(s1, strlen(funcname) + 2);
	    dwarf_line_op(s1, DW_LNE_hi_user - 1);
	    len = strlen(funcname) + 1;
	    for (i = 0; i < len; i++)
		dwarf_line_op(s1, funcname[i]);
	}
    }
    else
    {
        cstr_new (&debug_str);
        cstr_printf(&debug_str, "%s:%c", funcname, sym->type.t & VT_STATIC ? 'f' : 'F');
        tcc_get_debug_info(s1, sym->type.ref, &debug_str);
        put_stabs_r(s1, debug_str.data, N_FUN, 0, f->line_num, 0, cur_text_section, sym->c);
        cstr_free (&debug_str);
        tcc_debug_line(s1);
    }
}

/* put function size */
ST_FUNC void tcc_debug_funcend(TCCState *s1, int size)
{
    if (!s1->do_debug)
        return;
    tcc_debug_line(s1);
    tcc_debug_stabn(s1, N_RBRAC, size);
    if (s1->dwarf) {
        int func_sib = 0;
	Sym *sym = dwarf_info.func;
	int n_debug_info = tcc_get_dwarf_info(s1, sym->type.ref);

        dwarf_data1(dwarf_info_section,
	    sym->type.t & VT_STATIC ? DWARF_ABBREV_SUBPROGRAM_STATIC
				    : DWARF_ABBREV_SUBPROGRAM_EXTERNAL);
        if ((sym->type.t & VT_STATIC) == 0)
            dwarf_data1(dwarf_info_section, 1);
        dwarf_strp(dwarf_info_section, funcname);
        dwarf_uleb128(dwarf_info_section, dwarf_line.cur_file);
        dwarf_uleb128(dwarf_info_section, dwarf_info.line);
	tcc_debug_check_anon(s1, sym->type.ref, dwarf_info_section->data_offset);
        dwarf_data4(dwarf_info_section, n_debug_info - dwarf_info.start);
        dwarf_reloc(dwarf_info_section, section_sym, R_DATA_PTR);
#if PTR_SIZE == 4
        dwarf_data4(dwarf_info_section, func_ind); // low_pc
        dwarf_data4(dwarf_info_section, size); // high_pc
#else
        dwarf_data8(dwarf_info_section, func_ind); // low_pc
        dwarf_data8(dwarf_info_section, size); // high_pc
#endif
        func_sib = dwarf_info_section->data_offset;
        dwarf_data4(dwarf_info_section, 0); // sibling
        dwarf_data1(dwarf_info_section, 1);
#if defined(TCC_TARGET_I386)
        dwarf_data1(dwarf_info_section, DW_OP_reg5); // ebp
#elif defined(TCC_TARGET_X86_64)
        dwarf_data1(dwarf_info_section, DW_OP_reg6); // rbp
#elif defined TCC_TARGET_ARM
        dwarf_data1(dwarf_info_section, DW_OP_reg13); // sp
#elif defined TCC_TARGET_ARM64
        dwarf_data1(dwarf_info_section, DW_OP_reg29); // reg 29
#elif defined TCC_TARGET_RISCV64
        dwarf_data1(dwarf_info_section, DW_OP_reg8); // r8(s0)
#else
        dwarf_data1(dwarf_info_section, DW_OP_call_frame_cfa);
#endif
        tcc_debug_finish (s1, debug_info_root);
	dwarf_data1(dwarf_info_section, 0);
        write32le(dwarf_info_section->data + func_sib,
                  dwarf_info_section->data_offset - dwarf_info.start);
    }
    else
    {
        tcc_debug_finish (s1, debug_info_root);
    }
}


ST_FUNC void tcc_debug_extern_sym(TCCState *s1, Sym *sym, int sh_num, int sym_bind, int sym_type)
{
    if (!s1->do_debug)
        return;
    if (sym_type == STT_FUNC || sym->v >= SYM_FIRST_ANOM)
        return;
    if (s1->dwarf) {
        int debug_type;

        debug_type = tcc_get_dwarf_info(s1, sym);
	dwarf_data1(dwarf_info_section,
		    sym_bind == STB_GLOBAL
		    ? DWARF_ABBREV_VARIABLE_EXTERNAL
		    : DWARF_ABBREV_VARIABLE_STATIC);
	dwarf_strp(dwarf_info_section, get_tok_str(sym->v, NULL));
	dwarf_uleb128(dwarf_info_section, dwarf_line.cur_file);
	dwarf_uleb128(dwarf_info_section, file->line_num);
	tcc_debug_check_anon(s1, sym, dwarf_info_section->data_offset);
	dwarf_data4(dwarf_info_section, debug_type - dwarf_info.start);
	if (sym_bind == STB_GLOBAL)
	    dwarf_data1(dwarf_info_section, 1);
	dwarf_data1(dwarf_info_section, PTR_SIZE + 1);
	dwarf_data1(dwarf_info_section, DW_OP_addr);
	greloca(dwarf_info_section, sym, dwarf_info_section->data_offset,
		R_DATA_PTR, 0);
#if PTR_SIZE == 4
	dwarf_data4(dwarf_info_section, 0);
#else
	dwarf_data8(dwarf_info_section, 0);
#endif
    }
    else
    {
        Section *s = s1->sections[sh_num];
        CString str;

        cstr_new (&str);
        cstr_printf (&str, "%s:%c",
                get_tok_str(sym->v, NULL),
                sym_bind == STB_GLOBAL ? 'G' : func_ind != -1 ? 'V' : 'S'
                );
        tcc_get_debug_info(s1, sym, &str);
        if (sym_bind == STB_GLOBAL)
            tcc_debug_stabs(s1, str.data, N_GSYM, 0, NULL, 0, 0);
        else
            tcc_debug_stabs(s1, str.data,
                (sym->type.t & VT_STATIC) && data_section == s
                ? N_STSYM : N_LCSYM, 0, s, sym->c, 0);
        cstr_free (&str);
    }
}

ST_FUNC void tcc_debug_typedef(TCCState *s1, Sym *sym)
{
    if (!s1->do_debug)
        return;
    if (s1->dwarf) {
	int debug_type;

        debug_type = tcc_get_dwarf_info(s1, sym);
	if (debug_type != -1) {
	    dwarf_data1(dwarf_info_section, DWARF_ABBREV_TYPEDEF);
	    dwarf_strp(dwarf_info_section, get_tok_str(sym->v & ~SYM_FIELD, NULL));
	    dwarf_uleb128(dwarf_info_section, dwarf_line.cur_file);
	    dwarf_uleb128(dwarf_info_section, file->line_num);
	    tcc_debug_check_anon(s1, sym, dwarf_info_section->data_offset);
	    dwarf_data4(dwarf_info_section, debug_type - dwarf_info.start);
	}
    }
    else
    {
        CString str;
        cstr_new (&str);
        cstr_printf (&str, "%s:t",
                     (sym->v & ~SYM_FIELD) >= SYM_FIRST_ANOM
                     ? "" : get_tok_str(sym->v & ~SYM_FIELD, NULL));
        tcc_get_debug_info(s1, sym, &str);
        tcc_debug_stabs(s1, str.data, N_LSYM, 0, NULL, 0, 0);
        cstr_free (&str);
    }
}

/* ------------------------------------------------------------------------- */
/* for section layout see lib/tcov.c */

ST_FUNC void tcc_tcov_block_end(TCCState *s1, int line);

ST_FUNC void tcc_tcov_block_begin(TCCState *s1)
{
    SValue sv;
    void *ptr;
    unsigned long last_offset = tcov_data.offset;

    tcc_tcov_block_end (tcc_state, 0);
    if (s1->test_coverage == 0 || nocode_wanted)
	return;

    if (tcov_data.last_file_name == 0 ||
	strcmp ((const char *)(tcov_section->data + tcov_data.last_file_name),
		file->true_filename) != 0) {
	char wd[1024];
	CString cstr;

	if (tcov_data.last_func_name)
	    section_ptr_add(tcov_section, 1);
	if (tcov_data.last_file_name)
	    section_ptr_add(tcov_section, 1);
	tcov_data.last_func_name = 0;
	cstr_new (&cstr);
	if (file->true_filename[0] == '/') {
	    tcov_data.last_file_name = tcov_section->data_offset;
	    cstr_printf (&cstr, "%s", file->true_filename);
	}
	else {
	    getcwd (wd, sizeof(wd));
	    tcov_data.last_file_name = tcov_section->data_offset + strlen(wd) + 1;
	    cstr_printf (&cstr, "%s/%s", wd, file->true_filename);
	}
	ptr = section_ptr_add(tcov_section, cstr.size + 1);
	strcpy((char *)ptr, cstr.data);
#ifdef _WIN32
        normalize_slashes((char *)ptr);
#endif
	cstr_free (&cstr);
    }
    if (tcov_data.last_func_name == 0 ||
	strcmp ((const char *)(tcov_section->data + tcov_data.last_func_name),
		funcname) != 0) {
	size_t len;

	if (tcov_data.last_func_name)
	    section_ptr_add(tcov_section, 1);
	tcov_data.last_func_name = tcov_section->data_offset;
	len = strlen (funcname);
	ptr = section_ptr_add(tcov_section, len + 1);
	strcpy((char *)ptr, funcname);
	section_ptr_add(tcov_section, -tcov_section->data_offset & 7);
	ptr = section_ptr_add(tcov_section, 8);
	write64le (ptr, file->line_num);
    }
    if (ind == tcov_data.ind && tcov_data.line == file->line_num)
        tcov_data.offset = last_offset;
    else {
        Sym label = {0};
        label.type.t = VT_LLONG | VT_STATIC;

        ptr = section_ptr_add(tcov_section, 16);
        tcov_data.line = file->line_num;
        write64le (ptr, (tcov_data.line << 8) | 0xff);
        put_extern_sym(&label, tcov_section,
		       ((unsigned char *)ptr - tcov_section->data) + 8, 0);
        sv.type = label.type;
        sv.r = VT_SYM | VT_LVAL | VT_CONST;
        sv.r2 = VT_CONST;
        sv.c.i = 0;
        sv.sym = &label;
#if defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64 || \
    defined TCC_TARGET_ARM || defined TCC_TARGET_ARM64 || \
    defined TCC_TARGET_RISCV64
        gen_increment_tcov (&sv);
#else
        vpushv(&sv);
        inc(0, TOK_INC);
        vpop();
#endif
        tcov_data.offset = (unsigned char *)ptr - tcov_section->data;
        tcov_data.ind = ind;
    }
}

ST_FUNC void tcc_tcov_block_end(TCCState *s1, int line)
{
    if (s1->test_coverage == 0)
	return;
    if (line == -1)
        line = tcov_data.line;
    if (tcov_data.offset) {
	void *ptr = tcov_section->data + tcov_data.offset;
	unsigned long long nline = line ? line : file->line_num;

	write64le (ptr, (read64le (ptr) & 0xfffffffffull) | (nline << 36));
	tcov_data.offset = 0;
    }
}

ST_FUNC void tcc_tcov_check_line(TCCState *s1, int start)
{
    if (s1->test_coverage == 0)
	return;
    if (tcov_data.line != file->line_num) {
        if ((tcov_data.line + 1) != file->line_num) {
	    tcc_tcov_block_end (s1, -1);
	    if (start)
                tcc_tcov_block_begin (s1);
	}
	else
	    tcov_data.line = file->line_num;
    }
}

ST_FUNC void tcc_tcov_start(TCCState *s1)
{
    if (s1->test_coverage == 0)
	return;
    if (!s1->dState)
        s1->dState = tcc_mallocz(sizeof *s1->dState);
    memset (&tcov_data, 0, sizeof (tcov_data));
    if (tcov_section == NULL) {
        tcov_section = new_section(tcc_state, ".tcov", SHT_PROGBITS,
				   SHF_ALLOC | SHF_WRITE);
	section_ptr_add(tcov_section, 4); // pointer to executable name
    }
}

ST_FUNC void tcc_tcov_end(TCCState *s1)
{
    if (s1->test_coverage == 0)
	return;
    if (tcov_data.last_func_name)
        section_ptr_add(tcov_section, 1);
    if (tcov_data.last_file_name)
        section_ptr_add(tcov_section, 1);
}

ST_FUNC void tcc_tcov_reset_ind(TCCState *s1)
{
    tcov_data.ind = 0;
}

/* ------------------------------------------------------------------------- */
#undef last_line_num
#undef new_file
#undef section_sym
#undef debug_next_type
#undef debug_hash
#undef n_debug_hash
#undef debug_anon_hash
#undef n_debug_anon_hash
#undef debug_info
#undef debug_info_root
#undef dwarf_sym
#undef dwarf_line
#undef dwarf_info
#undef tcov_data
