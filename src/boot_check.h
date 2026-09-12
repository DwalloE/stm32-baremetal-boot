#ifndef BOOT_CHECK_H
#define BOOT_CHECK_H

/*
 * The firmware grades its own startup. Each function prints exactly one
 * line and returns 1 on pass. Pass and fail texts share no substring so a
 * scenario matching either cannot be fooled by the other:
 *   "boot: data ok, bss ok, sp ok"      vs   "boot INTEGRITY VIOLATION: ..."
 *   "vec: table at ..."                 vs   "vec MISMATCH: ..."
 */
int boot_check_print(void);
int vec_check_print(void);

/* The `map` shell command: the linker symbols and the live stack pointer. */
void boot_print_map(void);
/* The `vec` shell command: entries 0-15 of the table the hardware is using. */
void boot_print_vectors(void);

#endif
