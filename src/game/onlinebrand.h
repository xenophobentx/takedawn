#ifndef BO3_ONLINEBRAND_H
#define BO3_ONLINEBRAND_H
#include <stddef.h>
#include <stdint.h>
/* Return required UTF-16 units including NUL, or 0 for an unterminated
 * input or no match. If dst is non-null, write only when it all fits. */
size_t bo3_onlinebrand_replace(const uint16_t *src, size_t limit, uint16_t *dst, size_t capacity);
void bo3_onlinebrand_apply(void);
#endif
