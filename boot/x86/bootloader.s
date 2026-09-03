
org 0x7C00
bits 16

jmp short start
nop
db "MSWIN4.1"
dw 512
db 1
dw 1
db 2
dw 224
dw 2880
db 0xF0
dw 9
dw 18
dw 2

kernel_kilobytes dw 0x0
boot_drive_number db 0
db 0
dd 0
db 0
db 0
db 0x29
dd 0xAAAAAAAA
db "DUMMYBPB   "
db "FAT12   "

start:
	; This bootloader is at 0x7C00, but we don't know which combination of segment
	; and offset. This bootloader is meant to be loaded at 0x0000:0x7C00, so do a 
	; far jump to segment 0x0000.
	cli
	cld
	jmp 0:set_cs



set_cs:
	; Zero out the other segments. This allows for correct access of data.
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov gs, ax

	; Set the stack to 0x0000:0x0000, so it will wrap around to
	; 0x0000:0xFFFE and so on.
	mov ss, ax
	mov sp, ax

	; The BIOS puts the 'boot drive number' in DL. This can be passed to BIOS 
	; disk calls to select the correct disk.
	mov [boot_drive_number], dl

	; Clear the screen by calling the BIOS.
	mov ax, 0x3
	int 0x10

	; Load the rest of the bootloader, and the DemoFS table.
	xor eax, eax
	inc ax
	mov cx, 8
	mov di, 0x7C00 + 512
	call read_sector

	; Read the kernel's inode number, and size.
	mov eax, [0x7C00 + 512 * 8 + 12]
	mov ecx, eax
	
	; Inode number
	and eax, 0xFFFFFF
	
	; Size in kilobytes (must do two shifts to chop off the low bit,
	; or I guess you could do shr ebx, 23; and bl, 0xFE)
	shr ecx, 24
	shl ecx, 1
	mov [kernel_kilobytes], cx

	; Load a sector at at time to 0x1000:0x0000
	; ie. 0x10000. This allows sizes of approx. 448KB (our filesystem
	; only supports a kernel size of 255KB anyway).
	mov bx, 0xC00
	mov gs, bx
	xor di, di	

next_sector:
	push cx
	mov cx, 1
	call read_sector
	pop cx

	; Move to next sector
	inc eax
	
	; Move 512 bytes along ( = 0x200 bytes = offset change of 0x20)
	mov bx, gs
	add bx, 0x20
	mov gs, bx
	loop next_sector

	mov [boot_drive_number], dl
	jmp 0xC000
	
vga_pos dw 4
retry_count db 5
read_sector:
	mov [retry_count], byte 5

	; Put sector number in EAX. Put the number of sectors in CX.
	; Put the destination offset in DI. The segment will be GS.

	pushad
	push es

	; This attempts to read from an 'extended' hard drive (i.e. all
	; modern drives)
	mov [io_lba], eax
	mov ax, gs
	mov [io_segment], ax
	mov dl, byte [boot_drive_number]
	mov ah, 0x42
	mov si, disk_io_packet
	mov [disk_io_packet], byte 0x10
	mov [io_offset], di
	mov [io_count], cx
	;int 0x13
	
	; If succeeded, we can exit.
	;jnc skip_floppy_read
	
	; Otherwise we must call the older CHS method of reading (likely
	; from a floppy).

	;mov ah, 0x8
	;xor di, di			;guard against BIOS bugs
	;mov es, di
	;mov dl, byte [boot_drive_number]
	;int 0x13
	;jnc short .got_parameters

.retry_floppy:
	; assume floppy
	; 80 cylinders, 2 heads, and 18 sectors per track
	mov ch, 80
	mov cl, 18
	mov dh, 1
.got_parameters:
	inc dh				;BIOS returns one less than actual value

	dec dh		; @@@ TODO HACK GOOFY FIX FOR DODGY FLOPPY DRIVE
				; @@@ REMOVE THIS LINE WHEN FDD IS FIXED!!
				
	and cx, 0x3F		;NUM SECTORS PER CYLINDER IN CX
	mov bl, dh			
	xor bh, bh			;NUM HEADS IN BX
	
	lfs ax, [io_lba]	;first load [d_lba] into GS:AX
	mov dx, fs			;then copy GS to DX to make it DX:AX

	div cx

	inc dl
	mov cl, dl

	xor dx, dx
	div bx

	;LBA					0x2000
	;ABSOLUTE SECTOR:	CL	0x03
	;ABSOLUTE HEAD:		DL	0x0A
	;ABSOLUTE CYLINDER:	AX	0x08

	;get the low two bits of AH into the top 2 bits of CL
	and ah, 3
	shl ah, 6
	or cl, ah
						;SECTOR ALREADY IN CL
	mov ch, al			;CYL
	mov ax, [io_count]	;SECTOR COUNT
	mov ah, 0x02		;FUNCTION NUMBER
	mov dh, dl			;HEAD
	mov dl, [boot_drive_number]
	mov bx, [io_segment]
	mov es, bx
	mov bx, [io_offset]
	int 0x13
	jnc short skip_floppy_read

	;dec byte [retry_count]

	; need retry
	mov dl, [boot_drive_number]
	mov ah, 0
	int 0x13

	push cx
	xor cx, cx
	loop $
	loop $
	loop $
	loop $
	loop $
	loop $
	loop $
	loop $
	loop $
	loop $
	loop $
	loop $
	pop cx
	
	jmp .retry_floppy


skip_floppy_read:
	pop es
	popad
	

	ret



; The GDT. We are required to have this setup before we go into 32 bit mode, as
; it defines the segments we use in 32 bit mode.
align 8
gdt_start:
	dd 0
	dd 0
	
	dw 0xFFFF
	dw 0x0000
	db 0
	db 10011010b
	db 11001111b
	db 0

	dw 0xFFFF
	dw 0x0000
	db 0
	db 10010010b
	db 11001111b
	db 0
gdt_end:

gdtr:
	dw gdt_end - gdt_start - 1
	dd gdt_start

times 0x1BE - ($-$$) db 0

; Partition table
db 0x80					; bootable
db 0x01
db 0x01
db 0x01
db 0x0C					; pretend to be FAT32
db 0x01
db 0x01
db 0x01
dd 0					; start sector (we put a dummy VBR here)
dd 16384				; total sectors in partition

db 0x00					; bootable
db 0x01
db 0x01
db 0x01
db 0x0C					; pretend to be FAT32
db 0x01
db 0x01
db 0x01
dd 16384				; start sector (we put a dummy VBR here)
dd 100000    			; total sectors in partition

; A data packet we use to interface with the BIOS extended disk functions.
; We'll borrow the memory from the partition table
align 16
disk_io_packet:
	db 0x00
	db 0x00
io_count:
	dw 0x0000
io_offset:
	dw 0x0000
io_segment:
	dw 0x0000
io_lba:
	dd 0
	dd 0

times 0x1FE - ($-$$) db 0
dw 0xAA55

; Pretend to be a FAT32 partition by having a 'valid enough' VBR
; (this will never get used, it just might 'help' with the USB)

bits 16
jmp short vbr_dummy
nop
db "DEMOFS  "
dw 0x200
db 0x1
dw 0x187E
db 0x2
dw 0x0
dw 0x0
db 0xF8
dw 0x0
dw 0x0
dw 0x0
dd 0x1
dd 131072
dd 0x3C1
dw 0
dw 0
dd 2
dw 1
dw 6
db "DEMOFSDEMOFS"
db 0x80
db 0x00
db 0x28
dd 0xDEADC0DE

vbr_dummy:
	jmp 0x7C00

times (512 * 2 - 2) - ($-$$) db 0x90
dw 0xAA55

