global ArchSetupNewThreadEntry

; void ArchSetupNewThreadEntry(struct thr* thr, void(*entry)(void*), void* ctxt);

ArchSetupNewThreadEntry:
    ; Follow calling convention to let us touch any registers.
    push ebx
	push esi
	push edi
	push ebp

	; Grab the 3 arguments.
    mov eax, [esp + (4 + 1) * 4]        ; eax = thr
    mov ebx, [esp + (4 + 2) * 4]        ; ebx = entry
    mov ecx, [esp + (4 + 3) * 4]        ; ecx = ctxt

    ; Figure out the kernel stack pointer of the new thread.
    ; All tasks start in kernel mode, then user threads can jump to usermode.
    mov edx, [eax + 8]

    ; Push context first, then entry point - as entry point will get popped,
    ; and then it will read up the stack to find the context.
    mov [edx - 4], ecx  
    mov dword [edx - 8], ThreadRanOffEnd     ; "return address"
    mov [edx - 12], ebx

    ; The context switch code pops 4 register off the stack, so need to put
    ; those dummies on here.
    xor ebx, ebx
    mov [edx - 16], ebx
    mov [edx - 20], ebx
    mov [edx - 24], ebx
    mov [edx - 28], ebx

    sub edx, 28

    ; Save their stack back to the thread struct as the actual active stack
    ; pointer (not the kernel stack base used in TSS).
    mov [eax + 12], edx
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret


ThreadRanOffEnd:
    ; TODO: in the future just terminate it
    cli
    hlt
    mov eax, 0xDEAD0000
    mov ebx, 0xBEEF0000
    mov ecx, 0xAABBCCDD
    mov edx, 0xEEEEFFFF
    mov edi, 0x99999999
    mov esi, 0x88888888
    jmp $