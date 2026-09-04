
;   EAX = LBA of sector to read
;    CX = number of sectors to read
;    DL = drive to read
; BX:DI = destination

BiosReadSector:
    pusha
    push es
    mov [boot_drive], dl
    
    ; ================================================
    ; First try with the extended routine.
    ; ================================================
    mov [d_cnt], cx
    mov [d_add], di
    mov [d_seg], bx
    mov [d_lba], eax
    mov [d_hdr], word 0x0010
    mov ah, 0x42
	mov si, io_packet
	int 0x13
    jnc short .success

    ; ================================================
    ; If not, we need to use CHS. Try getting the disk
    ; geometry.
    ; ================================================
    mov ah, 0x8
	xor di, di			; guard against BIOS bugs
	mov es, di
    mov dl, [boot_drive]
	int 0x13
    jnc short .got_geometry

    
    ; ================================================
    ; Couldn't get geometry, likely a floppy drive 
    ; (geometry interrupt is not reliable on floppies).
    ; Assume normaly layout.
    ; ================================================
    mov cx, 80
	mov cl, 18
	mov dh, 1

.got_geometry:
    ; ================================================
    ; Do LBA to CHS
    ; ================================================


    dec dh		; @@@ TODO HACK GOOFY FIX FOR DODGY FLOPPY DRIVE
				; @@@ REMOVE THIS LINE WHEN FDD IS FIXED!!

	inc dh				;BIOS returns one less than actual value
	and cx, 0x3F		;NUM SECTORS PER CYLINDER IN CX
	mov bl, dh			
	xor bh, bh			;NUM HEADS IN BX
	lfs ax, [d_lba]	    ;first load [d_lba] into GS:AX
	mov dx, fs			;then copy GS to DX to make it DX:AX
	div cx
	inc dl
	mov cl, dl
	xor dx, dx
	div bx
	and ah, 3
	shl ah, 6
	or cl, ah
						;SECTOR ALREADY IN CL
	mov ch, al			;CYL
	mov ax, [d_cnt]     ;sector count
	mov ah, 0x02		;FUNCTION NUMBER
	mov dh, dl			;HEAD
	mov dl, [boot_drive]
	mov bx, [d_add]
	mov es, [d_seg]
.retry:
    pusha
	int 0x13
    jnc short .success2

    ; ================================================
    ; Retry on failure, after resetting the controller.
    ; ================================================
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13
    popa
    jmp short .retry
    
.success2:
    popa
.success:
    pop es
    popa
    ret

boot_drive db 0
align 8
io_packet:
d_hdr:  db	0x10
        db	0
d_cnt:	dw	1		; int 13 resets this to # of blocks actually read/written
d_add:	dw	0x0000	; memory buffer destination address (0:7c00)
d_seg:	dw	0x0000	; in memory page zero
d_lba:	dd	0		; put the lba to read in this spot
    	dd	0		; more storage bytes only for big lbas ( > 4 bytes )
