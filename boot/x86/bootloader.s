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

dw 0x0
db 0
db 0
dd 0
db 0
db 0
db 0x29
dd 0xAAAAAAAA
db "DUMMYBPB   "
db "FAT12   "

start:
    cli
    cld
    jmp 0:set_cs        ; Short jump saves a byte

set_cs:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov gs, ax
    mov ss, ax
    mov sp, ax              ; Stack at 0x0000:0x0000 (wraps to 0xFFFE)

    mov [boot_drive_number], dl

	; Clear screen (AH already zero from above)
    mov al, 3
    int 0x10

    ; --- STEP 1: Calculate Root Directory LBA ---
    mov al, [0x7C10]        ; NumFATs
    cbw
    mov bx, [0x7C16]        ; FATSz16
    mul bx                  ; DX:AX = NumFATs * FATSz16
    mov bx, [0x7C0E]        ; RsvdSecCnt
    add ax, bx              ; AX = Root Directory LBA
    
    ; --- STEP 2: Read First Sector of Root Directory ---
    movzx eax, ax           
    mov cx, 1               
    xor bx, bx 
    mov di, 0x7E00          
    call BiosReadSector     

    ; --- STEP 3: Extract Starting Cluster of 'SYSTEM' Folder ---
    mov ax, [0x7E1A]        
    push ax                 ; Save cluster temporarily

    ; --- STEP 4: Calculate First Data Sector ---
    mov al, [0x7C10]
    cbw
    mov bx, [0x7C16]
    mul bx
    mov bx, [0x7C0E]
    add ax, bx              ; AX = RootSec
    
    push ax                 
    mov ax, [0x7C11]        ; RootEntCnt
    shl ax, 5               ; Multiply by 32 bytes per entry
    mov bx, [0x7C0B]        ; BytesPerSec
    xor dx, dx
    div bx                  ; AX = Root Directory size in sectors
    mov bx, ax              
    pop ax                  
    add ax, bx              ; AX = First Data Sector
    mov bp, ax              ; Save First Data Sector in BP

    ; --- STEP 5: Map 'SYSTEM' Cluster to LBA Sector ---
    pop ax                  ; Restore 'SYSTEM' folder cluster
    sub ax, 2               
    xor bx, bx
    mov bl, [0x7C0D]        ; SecPerClust
    mul bx                  
    add ax, bp              
    movzx eax, ax           

    ; --- STEP 6: Read the 'SYSTEM' Folder Sector ---
    mov cx, 1               
    xor bx, bx
    mov di, 0x8200          
    call BiosReadSector

    ; --- STEP 7: Search for 'BIOS.SYS' in the SYSTEM Folder Sector ---
    mov si, 0x8200          
    mov cx, 16              

search_loop:
    mov al, [ds:si]
    test al, al
    jz short file_not_found 
    cmp al, 0xE5            
    je short next_entry

    push si
    push cx
    mov di, bios_sys_name   
    mov cx, 11              
    repe cmpsb              
    pop cx
    pop si
    je short found_bios_sys 

next_entry:
    add si, 32              
    dec cx
    jnz short search_loop

file_not_found:
    jmp short $             

found_bios_sys:
    ; --- STEP 8: Extract File Size & Convert Cluster to LBA ---
    mov ecx, [si + 0x1C]    ; ECX = File size in bytes

    mov ax, [si + 0x1A]     ; AX = Starting cluster
    dec ax
	dec ax  
    xor bx, bx
    mov bl, [0x7C0D]        
    mul bx                  
    add ax, bp              
    movzx eax, ax           ; EAX = Starting LBA of BIOS.SYS

    ; Convert from bytes to sectors
    add ecx, 511
    shr ecx, 9

	mov [boot_drive_number], dl
    mov di, 0xC000

.next_sector:
    push ecx
    mov cx, 1               
    xor bx, bx
    call BiosReadSector
    pop ecx
    add di, 512
    inc eax
    dec cx
    jnz short .next_sector
    jmp 0xC000

bios_sys_name: 
    db "BIOS    SYS"    

%include "boot/x86/int13h.s"

boot_drive_number:
    db 0

    times 510 - ($ - $$) db 0
    dw 0xAA55