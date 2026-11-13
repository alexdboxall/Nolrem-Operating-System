sudo qemu-system-i386 -cpu 486 -m 1M -audiodev coreaudio,id=audio0 -machine pcspk-audiodev=audio0 -monitor stdio -fda /dev/disk25 -serial file:log.txt -rtc base=localtime -d guest_errors,cpu_reset 

read -p "Press Enter to continue" </dev/tty