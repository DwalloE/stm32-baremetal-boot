#ifndef FAULT_H
#define FAULT_H

#include <stdint.h>

/* Called from the HardFault_Handler shim in startup.s with the stacked exception frame. Never returns. */
void hardfault_c(uint32_t *frame) __attribute__((noreturn));

/* The `fault` shell command: execute an undefined instruction on purpose. */
void fault_trigger_udf(void);

#endif
