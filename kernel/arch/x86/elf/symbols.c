#include <common.h>
#include <elf.h>
#include <panic.h>
#include <assert.h>
#include <vmm.h>
#include <string.h>
#include <module.h>
#include <heap.h>
#include <vfs.h>
#include <machine/x86.h>

/*
 * Whether a symbol gets registered. Used by BOTH the counting pass and the
 * registering pass, so they can never disagree about how many symbols there
 * are (CreateSymbolList() needs the exact count up front).
 */
static bool IsVisibleSymbol(const struct Elf32_Sym* symbol) {
    if (symbol->st_value == 0) {
        return false;
    }

    /* Skip "hidden" and "internal" symbols. */
    return (symbol->st_other & 3) == 0;
}

struct symbol_list* ArchLoadSymbols(struct file* file, size_t adjust, bool use_hash) {
    size_t size = (size_t) file->node->stat.st_size;

    if (size < sizeof(struct Elf32_Ehdr)) {
        return NULL;
    }

    /*
     * Read-only, kernel-only (flags == 0), demand-loaded from 'file' by the
     * page fault handler as we touch it. Not relocated, so reloc_base is 0.
     */
    void* map = AllocFileMemory(file, 0, size, 0, 0);
    if (map == NULL) {
        return NULL;
    }
    size_t mem = (size_t) map;

    struct Elf32_Ehdr* elf_header = (struct Elf32_Ehdr*) mem;
    if (!IsElfValid(elf_header) || elf_header->e_shoff == 0) {
        return NULL;
    }

    /*
     * Everything below is read straight out of the mapping, so a bogus offset
     * would otherwise run off the end of it and die as a page fault in a
     * non-paged area. Fail with a clear reason instead.
     */
    if ((uint64_t) elf_header->e_shoff + (uint64_t) elf_header->e_shnum * sizeof(struct Elf32_Shdr) > size
            || elf_header->e_shstrndx >= elf_header->e_shnum) {
        return NULL;
    }

    struct Elf32_Shdr* section_headers = (struct Elf32_Shdr*) (mem + elf_header->e_shoff);
    struct Elf32_Shdr* shstrtab = &section_headers[elf_header->e_shstrndx];
    if ((uint64_t) shstrtab->sh_offset + shstrtab->sh_size > size) {
        return NULL;
    }
    const char* section_names = (const char*) (mem + shstrtab->sh_offset);

    size_t symbol_table_offset = 0;
    size_t symbol_table_length = 0;
    size_t string_table_offset = 0;
    size_t string_table_length = 0;

    /*
     * Find the address and size of the symbol and string tables.
     */
    for (int i = 0; i < elf_header->e_shnum; ++i) {
        struct Elf32_Shdr* section = &section_headers[i];
        if (section->sh_name >= shstrtab->sh_size) {
            continue;
        }
        const char* name = section_names + section->sh_name;

        if (!strcmp(name, ".symtab")) {
            symbol_table_offset = section->sh_offset;
            symbol_table_length = section->sh_size;
        } else if (!strcmp(name, ".strtab")) {
            string_table_offset = section->sh_offset;
            string_table_length = section->sh_size;
        }
    }

    if (symbol_table_offset == 0 || string_table_offset == 0
            || symbol_table_length == 0 || string_table_length == 0
            || (uint64_t) symbol_table_offset + symbol_table_length > size
            || (uint64_t) string_table_offset + string_table_length > size) {
        return NULL;
    }

    struct Elf32_Sym* symbol_table = (struct Elf32_Sym*) (mem + symbol_table_offset);
    const char* string_table = (const char*) (mem + string_table_offset);
    size_t symbol_count = symbol_table_length / sizeof(struct Elf32_Sym);

    /*
     * Pass 1: count the visible symbols. The new symbol list has to be sized
     * up front. (The second pass re-reads the table; those pages are already
     * resident, or just get demand-loaded again if they were discarded.)
     */
    int total_syms = 0;
    for (size_t i = 0; i < symbol_count; ++i) {
        if (IsVisibleSymbol(&symbol_table[i])) {
            ++total_syms;
        }
    }

    struct symbol_list* sym_list = CreateSymbolList(total_syms);
    if (sym_list == NULL) {
        return NULL;
    }

    /*
     * Pass 2: register them. The index is the slot in the symbol list, i.e.
     * the Nth *visible* symbol, not the position in the ELF symbol table.
     *
     * NOTE: this assumes AddToSymbolList() copies the name (as AddSymbol() did
     * with strdup), because the mapping holding 'string_table' goes away below.
     */
    int next = 0;
    for (size_t i = 0; i < symbol_count; ++i) {
        const struct Elf32_Sym* symbol = &symbol_table[i];
        if (!IsVisibleSymbol(symbol)) {
            continue;
        }
        AddToSymbolList(sym_list, next++, string_table + symbol->st_name,
                        use_hash, symbol->st_value + adjust);
    }
    assert(next == total_syms);

    //UnmapVirt(mem, size);
    return sym_list;
}