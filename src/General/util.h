#ifndef _UTIL_H
#define _UTIL_H

#include "types.h"
#include "Primitives/rdRect.h"

#define util_FileExists_ADDR (0x0042F520)
#define util_files_exists_ADDR (0x0042F550)
#define util_unkcomparison1_ADDR (0x0042F700)
#define util_unkcomparison2_ADDR (0x0042F750)
#define util_unkcomparison3_ADDR (0x0042F7A0)
#define util_Weirdchecksum_ADDR (0x0042F810)

int util_FileExists(const char *fpath);
int util_FileExistsLowLevel(const char *fpath);
int util_RectsOverlap(const rdRect *a, const rdRect *b);
int util_RectsOverlapExclusive(const rdRect *a, const rdRect *b);
void util_RectUnion(rdRect *out, const rdRect *b);
uint32_t util_Weirdchecksum(uint8_t *data, int len, uint32_t last_hash);

#endif // _UTIL_H
