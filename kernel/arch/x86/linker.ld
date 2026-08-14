ENTRY(_start)
OUTPUT_FORMAT("elf32-i386")

SECTIONS
{
	. = 1M;
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
		*(.kuser)
		*(.kuserrodata)

		. = ALIGN(4096);
		*(.pageable)
		*(.pageablerodata)
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