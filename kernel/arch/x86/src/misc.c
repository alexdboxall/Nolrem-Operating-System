
void ArchIndicateSpinLoop(void) {
    asm volatile("pause");
}

void ArchIdle(void) {
    asm volatile("hlt");
}
