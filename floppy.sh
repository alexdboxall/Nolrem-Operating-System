
make osrelease
python tools/goofyfdcfix.py build/output/floppy.img
qemu-system-i386 -m 1M -audiodev coreaudio,id=audio0 -machine pcspk-audiodev=audio0 -monitor stdio -fda build/output/floppy.img -serial file:log.txt -rtc base=localtime -d guest_errors,cpu_reset 

read -p "Press Enter to continue" </dev/tty