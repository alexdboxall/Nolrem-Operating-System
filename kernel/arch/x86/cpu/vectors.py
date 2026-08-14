
output = '; Do not edit - this file was generated automatically!\n\n'
output += 'section .pageable\n'
output += 'global isr_vectors_first_33\n'
output += 'isr_vectors_first_33:\n'
for i in range(33):
    output += '    dd isrx{}\n'.format(i)
# the next ones you calculate by just adding 4 to each from isr_32
# except at 48, and every 32 from thereon, you add an extra 6.
#
# i.e. ISR32_ADDR + (N - 32) * 4 + ((N - 16) // 32) * 6

output += '\nsection .text\n'
output += 'extern InterruptCommonHandler\n'

for i in range(256):
    output += 'isrx{}:\n'.format(i)

    if not (i == 8 or (i >= 10 and i <= 14) or i == 17):
        if i < 128:
            output += '    push byte {}\n'.format(i)
        else:
            output += '    push byte {}\n'.format(i - 256)
        if i % 32 != 15:
            output += '    jmp short thunk{}\n\n'.format(i // 32)
        else:
            # it falls through to the thunk here!
            output += '\n'
    else:
        output += '    push byte {}\n'.format(i)
        output += '    jmp InterruptCommonHandler\n\n'.format(i // 32)

    if i % 32 == 15:
        output += 'thunk{}:\n'.format(i // 32)
        # we were sneaky, and in the isrx handlers just pushed the INT num,
        # but we needed to push an error code before it. the sneaky trick
        # is we don't care what the error code is, so we can just push the
        # same value *again!*. this gives error code = INT num both on the
        # stack.
        output += '    push dword [esp]\n'    
        output += '    jmp InterruptCommonHandler\n\n'

open('vectors.s', 'w').write(output)
