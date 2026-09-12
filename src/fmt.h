/*
 * fmt.h - the smallest output layer that can print a verdict line.
 *
 * No printf: newlib's printf and its dependents were ~50 KB of project 04's
 * image (docs/linker-map.md there). Here the whole firmware is a few KB and
 * the map file stays readable because there is nothing in it but our code.
 */
#ifndef FMT_H
#define FMT_H

#include <stdint.h>

void out_str(const char *s);
void out_char(char c);
void out_hex32(uint32_t v);      /* 0x%08x */
void out_dec(uint32_t v);        /* %u */
void out_nl(void);               /* \r\n */

#endif
