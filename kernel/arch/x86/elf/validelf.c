
#include <common.h>
#include <elf.h>

bool x86IsElfValid(struct Elf32_Ehdr* header) {
    if (header->e_ident[EI_MAG0] != ELFMAG0) return false;
    if (header->e_ident[EI_MAG1] != ELFMAG1) return false;
    if (header->e_ident[EI_MAG2] != ELFMAG2) return false;
    if (header->e_ident[EI_MAG3] != ELFMAG3) return false;

    /*
     * It must be a 32-bit, little-endian, version 1 image for the machine we
     * are running on. (The file type is deliberately not checked: this is
     * used for symbol tables, which don't care if it is an executable, an
     * object file or a shared object.)
     */
    if (header->e_ident[EI_CLASS] != ELFCLASS32) return false;
    if (header->e_ident[EI_DATA] != ELFDATA2LSB) return false;
    if (header->e_ident[EI_VERSION] != 1) return false;
    if (header->e_machine != EM_386) return false;
    if (header->e_version != 1) return false;

    return true;
}
