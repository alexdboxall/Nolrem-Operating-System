ENTRY(_start)
OUTPUT_FORMAT("elf32-i386")

SECTIONS
{
	. = 0x10000;
	.lowram.text : {
		*(.lowram.text)
	}
	
	. += 0xC0000000;
	
	.text ALIGN(16) : AT (ADDR (.text) - 0xC0000000)
	{
		*(.text .text.*)
	
		. = ALIGN(8);
		
		start_ctors = .;
        *(SORT(.ctors*))  	/* Note the "SORT" */
        end_ctors = .;

		*(.dtors)

		*(.rodata)
		*(.data)

		*(.cold_text)

		. = ALIGN(4096);
		__start_kuser = .;
		*(.kuser)
		*(.kuserrodata)
		__end_kuser = .;

		. = ALIGN(4096);
		__start_pageable = .;
		*(.pageable)
		*(.pageablerodata)
		__end_pageable = .;

		. = ALIGN(4096);
		__start_pageablekuser = .;
		*(.pageablekuser)
		*(.pageablekuserrodata)
		__end_pageablekuser = .;
	}
	
	.bss ALIGN(4096) : AT (ADDR (.bss) - 0xC0000000)
	{
		*(COMMON)
		*(.bss)
		*(.bootstrap_stack)
	}
	
	_kernel_end = .;
	
	/DISCARD/ :
    {
        *(.comment)
    }
}