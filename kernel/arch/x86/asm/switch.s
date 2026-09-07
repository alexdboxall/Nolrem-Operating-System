
;
; x86/asm/interrupt.s - Interrupt Handling
;
; Provides the common handler function for all of the different interrupts the
; CPU will receive. The specific handlers are generated in x86/cpu/vectors.py.
;

extern GetCpu
global ArchSwitchThread
ArchSwitchThread:
    ; The old and new threads are passed in on the stack as arguments, in that order.

	; The calling convention we use already saves EAX, ECX and EDX whenever a
	; function (e.g. ArchSwitchThread) is called. Therefore, we only need to save the other four.
	push ebx
	push esi
	push edi
	push ebp

	; We are now free to trash the general purpose registers (except ESP),
	; so we can now load the current task using the argument.

    ; First we have to save the old stack pointer. The old thread was the first
    ; argument, and we just pushed 4 things to the stack. The first argument gets
    ; pushed last, so read back 5 places. Also load the new thread's address in.

    mov edi, [esp + (4 + 1) * 4]        ; edi = old_thread
    mov esi, [esp + (4 + 2) * 4]        ; esi = new_thread

	mov [edi + 12], esp                  ; old_thread->stack_pointer = esp

    mov esp, [esi + 12]                  ; esp = new_thread->stack_pointer

    ; ESI is callee-saved, so no need to do anything here. We only need ESI and ESP
    ; at this point, so it's all good.

    call GetCpu                         ; eax = GetCpu()
	
	; Get the top of the kernel stack, which needs to go in the TSS for
	; user to kernel switches.
    mov ebx, [esi + 8]                  ; ebx = new_thread->kernel_stack_top
	
	; The third entry in current_cpu is a pointer to CPU specific data.
    mov ecx, [eax + 8]                  ; ecx = GetCpu()->platform_specific

	; The first entry in the CPU specific data is the TSS pointer
    mov edx, [ecx + 0]                  ; edx = GetCpu()->platform_specific->tss

	; Load the TSS's ESP0 with the new thread's stack
    mov [edx + 4], ebx

	; Now we have the new thread's stack, we can just pop off the state
	; that would have been pushed when it was switched out.
	pop ebp
	pop edi
	pop esi
	pop ebx

	ret

