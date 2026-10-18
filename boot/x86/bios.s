;
; This file is quite the circus. But that's alright (for now at least) - the
; whole point of splitting up the bootloader into a generic C part and a
; platform specific helper file is to ensure that all the 'clowning around' only
; needs to be done one per platform, and then it can just be hidden away behind
; the curtain.
;

; 0x500  - 0x5FF		INITIAL RAM MAP (BIOS FORMAT)
; 0x900  - 0x93F		BOOTSTRAP GDT
; 0x6F?? - 0x7000		REAL MODE (FIRMWARE.LIB STACK)
; 0x7C00 - 0x7DFF		STAGE 1/2 BOOTLOADER CODE/DATA
; 0x8000 - 0xBFFF		BOOTLOAD.EXE CODE/DATA
; 0xC000 - 0xCFFF		BIOS.SYS CODE/DATA
; 0xD000 - 0xDFFF		BIOS.SYS SECTOR BUFFER
; 0xF000 - 0xFFFF		ALL (PRE-KERNEL) STACKS

org 0xC000
bits 16

start:
	mov ax, 0
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	mov sp, ax

	mov [boot_drive], dl

	; Disable blink
	mov ax, 0x1003
	xor bx, bx
	int 0x10

	call generate_memory_map

	; Load a GDT
	xor eax, eax
	mov bx, 0xFFFF

	mov [fs:0x900], eax
	mov [fs:0x904], eax

	;32 bit p-mode code
	mov [fs:0x908], word bx
	mov [fs:0x90A], word ax
	mov [fs:0x90C], word 0x9A00
	mov [fs:0x90E], word 0xCF
	
	;32 bit p-mode code
	mov [fs:0x910], word bx
	mov [fs:0x912], word ax
	mov [fs:0x914], word 0x9200
	mov [fs:0x916], word 0xCF
	
	;16 bit p-mode code
	mov [fs:0x918], word bx
	mov [fs:0x91A], word ax
	mov [fs:0x91C], word 0x9A00
	mov [fs:0x91E], word 0x0F

	;16 bit p-mode data
	mov [fs:0x920], word bx
	mov [fs:0x922], word ax
	mov [fs:0x924], word 0x9200
	mov [fs:0x926], word 0x0F

	mov [fs:0x928], word 0x27
	mov [fs:0x92A], dword 0x900

	mov eax, 0x928
	lgdt [eax]        ; load GDT into GDTR

    mov eax, cr0	
    or eax, 1
    mov cr0, eax

    jmp 0x8:protected_mode_main

	hlt
	jmp $

; The number of entries will be stored at 0x500, the map will be put at 0x504
; From here:
;		https://wiki.osdev.org/Detecting_Memory_(x86)
generate_memory_map:
    mov di, 0x504          ; Set di to 0x8004. Otherwise this code will get stuck in `int 0x15` after some entries are fetched 
	xor ebx, ebx		; ebx must be 0 to start
	xor bp, bp		; keep an entry count in bp
	mov edx, 0x0534D4150	; Place "SMAP" into edx
	mov eax, 0xe820
	mov [es:di + 20], dword 1	; force a valid ACPI 3.X entry
	mov ecx, 24		; ask for 24 bytes
	int 0x15
	stc
	jc short .not_supported	; carry set on first call means "unsupported function"
	;mov edx, 0x0534D4150	; Some BIOSes apparently trash this register?
	;cmp eax, edx		; on success, eax must have been reset to "SMAP"
	;jne short .failed
	;test ebx, ebx		; ebx = 0 implies list is only 1 entry long (worthless)
	;je short .failed
	jmp short .jmpin
.e820lp:
	mov eax, 0xe820		; eax, ecx get trashed on every int 0x15 call
	mov [es:di + 20], dword 1	; force a valid ACPI 3.X entry
	mov ecx, 24		; ask for 24 bytes again
	int 0x15
	jc short .e820f		; carry set means "end of list already reached"
	mov edx, 0x0534D4150	; repair potentially trashed register
.jmpin:
	jcxz .skipent		; skip any 0 length entries
	cmp cl, 20		; got a 24 byte ACPI 3.X response?
	jbe short .notext
	test byte [es:di + 20], 1	; if so: is the "ignore this data" bit clear?
	je short .skipent
.notext:
	mov ecx, [es:di + 8]	; get lower uint32_t of memory region length
	or ecx, [es:di + 12]	; "or" it with upper uint32_t to test for zero
	jz .skipent		; if length uint64_t is 0, skip entry
	inc bp			; got a good entry: ++count, move to next storage spot
	add di, 24
.skipent:
	test ebx, ebx		; if ebx resets to 0, list is complete
	jne short .e820lp
.e820f:
	mov [0x500], bp	; store the entry count
.failed:
	ret

.not_supported:
	; ========================
	; First get low memory in KB.
	; ========================
	int 0x12
	movzx eax, ax
	shl eax, 10

	mov [0x500], dword 1
	mov di, 0x504

	; Starts at addr 0
	mov [di + 0], dword 0
	mov [di + 4], dword 0

	; Length
	mov [di + 8], eax
	mov [di + 12], dword 0

	; Type
	mov [di + 16], dword 1
	add di, 24

	; ========================
	; This gets contiguous RAM in KB from 1MB to 64MB.
	; ========================
	mov ah, 0x88
	int 0x15
	jc short .failed

	; check if for length = 0 and skip adding entry if so
	test ax, ax
	jz short .failed

	; Convert KB to bytes
	movzx eax, ax	
	shl eax, 10

	inc dword [0x500]
	mov [di + 0], dword 0x100000
	mov [di + 4], dword 0

	mov [di + 8], eax
	mov [di + 12], dword 0

	mov [di + 16], dword 1

	ret

enableA20:
	cli
	pusha

    call    wait_input
    mov     al,0xAD
    out     0x64,al		; disable keyboard
    call    wait_input

    mov     al,0xD0
    out     0x64,al		; tell controller to read output port
    call    wait_output

    in      al,0x60
    push    eax		; get output port data and store it
    call    wait_input

    mov     al,0xD1
    out     0x64,al		; tell controller to write output port
    call    wait_input

    pop     eax
    or      al,2		; set bit 1 (enable a20)
    out     0x60,al		; write out data back to the output port

    call    wait_input
    mov     al,0xAE		; enable keyboard
    out     0x64,al

    call    wait_input
popa
    ret

wait_input:
    in      al,0x64
    test    al,2
    jnz     wait_input
    ret

wait_output:
    in      al,0x64
    test    al,1
    jz      wait_output
    ret

bits 32

protected_mode_main:
	cli
	mov	ax, 0x10
	mov	ds, ax
	mov	ss, ax
	mov	es, ax
	mov esp, 0x10000

	mov cx, [0x500]
	mov [num_ram_entries], cx

	mov edi, proper_ram_table
	mov esi, 0x504
.ram_table_loop:
	; base address
	mov eax, [esi]
	mov [edi], eax
	mov eax, [esi + 4]
	mov [edi + 4], eax
	add edi, 8
	add esi, 8

	; length
	mov eax, [esi]
	mov [edi], eax
	mov eax, [esi + 4]
	mov [edi + 4], eax
	add edi, 8
	add esi, 8

	mov eax, [esi]
	cmp eax, 1
	je .goodram
	cmp eax, 2
	je .acpi

	mov eax, 2
	jmp .x

.goodram:
	mov eax, 0
	jmp .x

.acpi:
	mov eax, 1

.x:
	mov [edi], eax
	add edi, 8
	add esi, 8

	dec cx
	jnz .ram_table_loop

	; Parse the BPB and work out whether this is FAT12 or FAT16
	call fat_init
	jnc .fs_ok
	mov esi, STR_bad_fs
	call dbgprint
	cli
.fs_halt:
	hlt
	jmp .fs_halt
.fs_ok:

	push dword 0x8000
	push bootload_path
	call read_file

	add esp, 8
	push firmware_info
	call 0x8000


STR_reached_protected_mode db "Reached protected mode...", 0xA, 0
STR_processed_ram_table db "Processed RAM table...", 0xA, 0
STR_read_file db "Read file...", 0xA, 0
STR_bad_fs db "Unsupported or missing FAT12/FAT16 volume", 0xA, 0

; IN: ESI = string, null terminated
cur_pos_x db 0
cur_pos_y db 0
dbgprint:
    pusha
.print_loop:
    mov al, [esi]
    test al, al
    jz .done                 ; null terminator -> done
    cmp al, 0x0A              ; '\n' ?
    je .do_newline
    xor ebx, ebx
    mov bl, [cur_pos_y]
    imul ebx, ebx, 80         ; ebx = y*80
    xor ecx, ecx
    mov cl, [cur_pos_x]
    add ebx, ecx              ; ebx = y*80 + x
    shl ebx, 1                ; byte offset (2 bytes per cell)
    mov edi, 0xB8000
    add edi, ebx
    mov ah, 0x07               ; light grey on black
    mov [edi], ax              ; al = char, ah = attribute
    inc byte [cur_pos_x]
    jmp .advance
.do_newline:
    mov byte [cur_pos_x], 0
    inc byte [cur_pos_y]
.advance:
    inc esi
    jmp .print_loop
.done:
    popa
    ret

bootload_path db "System/bootload.exe", 0

firmware_info:
	num_ram_entries: dd 0
	dd proper_ram_table 
	dd 0x100000
	dd 0
	db "System/kernel.exe"
	times (32 - 17) db 0
	dd putchar
	dd check_key
	dd reboot
	dd sleep_100ms
	dd get_file_size
	dd read_file
	dd exit_firmware

putchar:
	mov eax, [esp + 4]	; x
	mov ebx, [esp + 8]	; y
	mov ecx, [esp + 12]	; char
	mov edx, [esp + 16]	; col
	
	shl ebx, 2			; y * 4
	add ebx, [esp + 8]	; y * 5
	shl ebx, 4			; y * 80
	add ebx, eax		; y * 80 + x
	add ebx, ebx		; 2 * (y * 80 + x)
	add ebx, 0xB8000
	mov [ebx], cl
	mov [ebx + 1], dl
	ret

check_key:
	mov [realModeCommand], byte 0
	call SWITCH_TO_REAL
	ret

reboot:
	jmp 0xFFFF:0
	ret

sleep_100ms:
	mov [realModeCommand], byte 1
	call SWITCH_TO_REAL
	ret

get_file_size:
	pusha
	mov esi, [esp + 4 + 8 * 4]
	call fat_find_file
	jc fail_generic1

	mov eax, [esp + 8 + 8 * 4]
	mov [eax], ebx
	popa
	xor eax, eax
	ret

read_file:
	pusha
	mov esi, [esp + 4 + 8 * 4]
	call fat_find_file
	jc fail_generic2

	add ebx, 511
	shr ebx, 9			; ebx = number of sectors left to read
	mov edi, [esp + 8 + 8 * 4]
.next_cluster:
	test ebx, ebx
	jz .done
	cmp eax, 2
	jb fail_generic2		; chain ended before the whole file was read

	push eax			; remember the cluster for the FAT lookup
	sub eax, 2
	movzx ecx, byte [fat_spc]
	imul eax, ecx
	add eax, [fat_data_lba]		; eax = first sector of this cluster
	mov edx, ecx			; edx = sectors left in this cluster
.next_sector:
	call read_vol_sector		; preserves all registers

	mov ecx, 512
	mov esi, 0xD000
	cld
	rep movsb

	inc eax
	dec ebx
	jz .last
	dec edx
	jnz .next_sector

	pop eax
	call fat_next_cluster
	jmp .next_cluster

.last:
	add esp, 4			; drop the saved cluster
.done:
	popa
	xor eax, eax
	ret

fail_generic1:
	popa
	mov eax, 1
	ret

fail_generic2:
	popa
	mov eax, 1
	ret

exit_firmware:
	ret

STR_fail_generic1_reached db "Could not find file (op: size)", 0xA, 0
STR_fail_generic2_reached db "Could not find file (op: read)", 0xA, 0

; ============================================================================
; FAT12 / FAT16
;
; Assumes 512 byte sectors. FAT32 (and anything else) is rejected by fat_init.
; Names are matched as 8.3 short names, case insensitively. Long file name
; entries are skipped, so a file needs a valid short name on disk.
; ============================================================================

; Sets up the fat_* variables from the BPB at the start of the volume, and
; auto-detects FAT12 vs FAT16 from the cluster count (as per the MS spec).
; OUT:
;	CF clear = OK
;	CF set   = not a FAT12/FAT16 volume
fat_init:
	pusha
	xor eax, eax
	call read_vol_sector		; volume boot sector -> 0xD000

	mov esi, 0xD000
	cmp word [esi + 11], 512	; bytes per sector
	jne .bad

	movzx eax, byte [esi + 13]	; sectors per cluster
	test al, al
	jz .bad
	mov [fat_spc], al

	movzx ebx, word [esi + 14]	; reserved sectors
	mov [fat_start_lba], ebx

	movzx eax, word [esi + 22]	; sectors per FAT (0 on FAT32)
	test eax, eax
	jz .bad
	movzx ecx, byte [esi + 16]	; number of FATs
	imul eax, ecx
	add ebx, eax			; ebx = root directory LBA
	mov [fat_root_lba], ebx

	movzx eax, word [esi + 17]	; root directory entries
	shl eax, 5			; * 32 bytes each
	add eax, 511
	shr eax, 9
	mov [fat_root_sectors], eax
	add ebx, eax			; ebx = first data sector (cluster 2)
	mov [fat_data_lba], ebx

	movzx eax, word [esi + 19]	; total sectors (16 bit)
	test eax, eax
	jnz .have_total
	mov eax, [esi + 32]		; total sectors (32 bit)
.have_total:
	sub eax, ebx			; eax = number of data sectors
	movzx ecx, byte [fat_spc]
	xor edx, edx
	div ecx				; eax = number of clusters

	cmp eax, 4085
	jb .is12
	cmp eax, 65525
	jae .bad			; that many clusters = FAT32
	mov byte [fat_is16], 1
	jmp .ok
.is12:
	mov byte [fat_is16], 0
.ok:
	popa
	clc
	ret
.bad:
	popa
	stc
	ret

; IN:
; 	ESI = path to file, '/' separated, null terminated
; OUT:
;	CF clear: EAX = first cluster of file, EBX = length of file
;	CF set:   not found (or the path is a directory)
fat_find_file:
	cld
	xor edx, edx		; directory to search, 0 = the root directory

.next_segment:
	; Turn the next path component into an 11 byte, space padded, upper
	; case 8.3 name in segment_buffer
	mov edi, segment_buffer
	mov ecx, 11
	mov al, ' '
	rep stosb
	mov edi, segment_buffer
	mov ecx, 8		; space left in the current field (name, then extension)

.get:
	lodsb
	cmp al, '/'
	je .got_segment
	cmp al, 0
	je .got_segment
	cmp al, '.'
	je .ext
	cmp al, 'a'
	jb .store
	cmp al, 'z'
	ja .store
	sub al, 'a' - 'A'
.store:
	test ecx, ecx
	jz .not_found		; component doesn't fit in 8.3
	stosb
	dec ecx
	jmp .get

.ext:
	mov edi, segment_buffer + 8
	mov ecx, 3
	jmp .get

.got_segment:
	push esi
	call fat_search_dir
	pop esi
	jc .not_found

	cmp byte [esi - 1], 0	; what terminated this component?
	je .last_segment

	; More path to go, so this one has to be a directory
	test cl, 0x10
	jz .not_found
	mov edx, eax		; descend into it
	jmp .next_segment

.last_segment:
	test cl, 0x10
	jnz .not_found		; a directory, not a file
	clc
	ret

.not_found:
	stc
	ret

; Searches one directory for the 8.3 name in segment_buffer.
; IN:
;	EDX = first cluster of the directory (0 = fixed root directory)
; OUT:
;	CF clear: EAX = first cluster, EBX = length, ECX = attribute byte
;	CF set:   not found
; Clobbers EDX, ESI, EDI, EBP and the sector buffer.
fat_search_dir:
	cld
	test edx, edx
	jnz .cluster

	; Fixed root directory: one contiguous run of sectors
	mov eax, [fat_root_lba]
	mov ebp, [fat_root_sectors]
	jmp .scan_run

.cluster:
	cmp edx, 2
	jb .fail
	mov eax, edx
	sub eax, 2
	movzx ecx, byte [fat_spc]
	mov ebp, ecx
	imul eax, ecx
	add eax, [fat_data_lba]

.scan_run:
	; EAX = sector, EBP = sectors left in this run
	call read_vol_sector
	mov edi, 0xD000

.entry:
	cmp byte [edi], 0
	je .fail			; 0x00 = no more entries
	cmp byte [edi], 0xE5
	je .next_entry			; deleted
	test byte [edi + 11], 0x08
	jnz .next_entry			; volume label / long file name entry

	mov esi, segment_buffer
	mov ecx, 11
	push edi
	repe cmpsb
	pop edi
	je .found

.next_entry:
	add edi, 32
	cmp edi, 0xD200
	jb .entry

	inc eax
	dec ebp
	jnz .scan_run

	; Ran out of sectors in this run. The root directory is a single run,
	; but subdirectories continue along their cluster chain.
	test edx, edx
	jz .fail
	mov eax, edx
	call fat_next_cluster
	mov edx, eax
	test edx, edx
	jnz .cluster

.fail:
	stc
	ret

.found:
	movzx ecx, byte [edi + 11]	; attributes
	movzx eax, word [edi + 26]	; first cluster
	mov ebx, [edi + 28]		; file size
	clc
	ret

; IN:
;	EAX = cluster
; OUT:
;	EAX = next cluster in the chain, or 0 if this was the last one (or the
;	      FAT entry is free/bad)
; Preserves everything else, but overwrites the sector buffer.
fat_next_cluster:
	push ebx
	push ecx
	push edx

	mov ebx, eax			; ebx = cluster
	cmp byte [fat_is16], 0
	jne .fat16

	; FAT12: 12 bit entries at byte offset cluster * 3 / 2
	mov ecx, ebx
	shr ecx, 1
	add ecx, ebx			; ecx = byte offset into the FAT
	mov eax, ecx
	shr eax, 9
	add eax, [fat_start_lba]	; sector holding the low byte
	and ecx, 511
	call read_vol_sector
	movzx edx, byte [0xD000 + ecx]	; low byte
	cmp ecx, 511
	je .straddle
	movzx ecx, byte [0xD000 + ecx + 1]	; high byte, same sector
	jmp .combine
.straddle:
	; Entry is split across two sectors: high byte is first byte of the next
	inc eax
	call read_vol_sector
	movzx ecx, byte [0xD000]
.combine:
	shl ecx, 8
	or ecx, edx
	mov eax, ecx
	test bl, 1
	jz .even
	shr eax, 4			; odd cluster: top 12 bits
	jmp .got12
.even:
	and eax, 0xFFF			; even cluster: bottom 12 bits
.got12:
	mov ecx, 0xFF7
	jmp .range

.fat16:
	mov eax, ebx
	shr eax, 8			; 256 entries per sector
	add eax, [fat_start_lba]
	call read_vol_sector
	and ebx, 255
	movzx eax, word [0xD000 + ebx * 2]
	mov ecx, 0xFFF7

.range:
	cmp eax, ecx			; bad cluster or end of chain marker
	jae .end
	cmp eax, 2
	jae .out			; valid data cluster
.end:
	xor eax, eax
.out:
	pop edx
	pop ecx
	pop ebx
	ret

; EAX = LBA relative to the start of the FAT volume
; output at 0xD000
; Unlike real_read_sector, this preserves all registers.
read_vol_sector:
	pusha
	add eax, [fat_vol_lba]
	call real_read_sector
	popa
	ret

; fat_vol_lba is the absolute LBA the volume starts at. It's 0 for a floppy /
; unpartitioned disk. If the stage 1 bootsector ever boots from a partition,
; set this to the partition's start LBA before fat_init runs.
fat_vol_lba dd 0
fat_start_lba dd 0		; first FAT (volume relative)
fat_root_lba dd 0		; fixed root directory (volume relative)
fat_root_sectors dd 0
fat_data_lba dd 0		; cluster 2 (volume relative)
fat_spc db 0			; sectors per cluster
fat_is16 db 0			; 0 = FAT12, 1 = FAT16

segment_buffer:
	times 11 db 0

; EAX = LBA (absolute)
; output at 0xD000
real_read_sector:
	mov [realModeCommand], byte 2
	mov [realModeData1], eax
	call SWITCH_TO_REAL
	ret

rmSwitchyStack dd 0

; 0 = read key
; 1 = wait 100ms
; 2 = read sector
realModeCommand db 0

realModeData1 dd 0
realModeData2 dd 0
realModeRet1 dd 0
realModeRet2 dd 0

SWITCH_TO_REAL:
	push edi
	push esi
	push ebp
	mov [realModeData1], eax
	mov [realModeData2], ebx

	cli ; 8.9.2. Step 1.

	mov [rmSwitchyStack], esp

	mov eax,cr0 ; 8.9.2. Step 2.
	and eax,0x7FFFFFFF	;0x7FFFFFFF
	mov cr0,eax

	jmp 0x18:prot16


[BITS 16]

prot16:
	mov ax,0x0020 ; 8.9.2. Step 4.
	mov ds,ax
	mov es,ax
	mov fs,ax
	mov gs,ax
	mov ss,ax

	mov sp, 0x7000

	mov eax,cr0 ; 8.9.2. Step 2.
	and al,0xFE	
	mov cr0,eax	;FREEZE!

	jmp word 0:real16		; 8.9.2. Step 7.

align 16
bits 16
real16:
	cli
	mov ax, 0
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

	mov sp, 0x7000

	mov al, [realModeCommand]
	cmp al, 0
	je bios_read_key
	cmp al, 1
	je bios_wait_100ms
	cmp al, 2
	je bios_read_sector

	mov [realModeRet1], dword -1
	jmp goBackHome

bios_read_key:
	mov [realModeRet1], dword 0

	mov ah, 1
	int 0x16
	jz goBackHome

	; we got a key!
	; AL has ASCII, AH has scancode

	mov ah, 0
	int 0x16
	
	test al, al
	jz .special_key
	movzx eax, al
	mov [realModeRet1], eax
	jmp goBackHome

.special_key:
	cmp ah, 0x48
	je .up
	cmp ah, 0x4D
	je .right
	cmp ah, 0x4B
	je .left
	cmp ah, 0x50
	je .right
	jmp goBackHome

.up:
	mov [realModeRet1], dword 1
	jmp goBackHome
.down:
	mov [realModeRet1], dword 2
	jmp goBackHome
.left:
	mov [realModeRet1], dword 3
	jmp goBackHome
.right:
	mov [realModeRet1], dword 4
	jmp goBackHome

bios_wait_100ms:
	mov ah, 0x86
	mov cx, 0x1
	mov dx, 34464		; 34,464 + 1 * 65536 = 100,000us = 100ms
	int 0x15
	jmp goBackHome


%include "int13h.s"

bios_read_sector:
	mov eax, [realModeData1]
	mov cx, 1				; 1 sector
	mov bx, 0xD00			; Read to:
	xor di, di				; 0D00:0000 = 0xD000
	mov dl, [boot_drive]
	call BiosReadSector
	jmp goBackHome

goBackHome:
	cli

	mov [realModeRet1], eax
	mov [realModeRet2], ebx

	xor ax, ax 
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov fs, ax

	cli
	mov [fs:0x900], dword 0
	mov [fs:0x904], dword 0

	;32 bit p-mode code
	mov [fs:0x908], word 0xFFFF
	mov [fs:0x90A], word 0
	mov [fs:0x90C], word 0x9A00
	mov [fs:0x90E], word 0xCF
	
	;32 bit p-mode code
	mov [fs:0x910], word 0xFFFF
	mov [fs:0x912], word 0
	mov [fs:0x914], word 0x9200
	mov [fs:0x916], word 0xCF
	
	;16 bit p-mode code
	mov [fs:0x918], word 0xFFFF
	mov [fs:0x91A], word 0
	mov [fs:0x91C], word 0x9A00
	mov [fs:0x91E], word 0x0F

	;16 bit p-mode data
	mov [fs:0x920], word 0xFFFF
	mov [fs:0x922], word 0
	mov [fs:0x924], word 0x9200
	mov [fs:0x926], word 0x0F

	mov [fs:0x928], word 0x27
	mov [fs:0x92A], dword 0x900

	mov eax, 0x928
	lgdt [eax]        ; load GDT into GDTR

	mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp .flush2
	.flush2:

	cli
	mov eax, cr0 
	or al, 1		; set PE (Protection Enable) bit in CR0 (Control Register 0)
	mov cr0, eax

	jmp 0x8:protected_mode_return ; + 0x7E00

BITS 32
protected_mode_return:
	cli

	mov	ax, 0x10
	mov	ds, ax
	mov	ss, ax
	mov	es, ax

	mov esp, [rmSwitchyStack]
	mov eax, [realModeRet1]
	mov ebx, [realModeRet2]

	pop ebp
	pop esi
	pop edi
	ret

proper_ram_table: