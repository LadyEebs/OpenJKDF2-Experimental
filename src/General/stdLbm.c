#include "stdLbm.h"

#include "General/stdBitmap.h"
#include "Win95/stdDisplay.h"
#include "Win95/std.h"
#include "stdPlatform.h"
#include "jk.h"

// PackBits RLE compressor state
static int stdLbm_runCount;        // 0x56c154
static char stdLbm_lastByte;       // 0x56c158
static int stdLbm_outputCount;     // 0x56c15c
static char *stdLbm_countPtr;      // 0x56c160 - pointer to current count byte
static char *stdLbm_writePtr;      // 0x56c164 - pointer to output write position
static int stdLbm_state;           // 0x56c168 - 0=literal, 1=run, 2=transition, 3=flush

void stdLbm_Compress(char byte)
{
    switch ( stdLbm_state )
    {
        case 0: // Literal mode
            if ( byte == stdLbm_lastByte )
                stdLbm_runCount++;
            else
                stdLbm_runCount = 0;

            if ( stdLbm_runCount > 2 )
            {
                // Switch to run mode
                stdLbm_state = 1;
                *stdLbm_countPtr -= 2;
                stdLbm_countPtr = stdLbm_writePtr - 2;
                stdLbm_lastByte = byte;
                *stdLbm_countPtr = -2;
                stdLbm_writePtr[-1] = byte;
                return;
            }

            (*stdLbm_countPtr)++;
            *stdLbm_writePtr = byte;
            stdLbm_writePtr++;

            if ( *stdLbm_countPtr == 0x7F )
                stdLbm_state = 3; // Flush

            stdLbm_lastByte = byte;
            stdLbm_outputCount++;
            return;

        case 1: // Run mode
            if ( byte != stdLbm_lastByte )
            {
                stdLbm_lastByte = byte;
                stdLbm_state = 2; // Transition
                return;
            }
            {
                char c = *stdLbm_countPtr;
                (*stdLbm_countPtr)--;
                if ( (char)(c - 1) == -127 )
                {
                    stdLbm_state = 3; // Flush
                    stdLbm_lastByte = byte;
                    return;
                }
            }
            stdLbm_lastByte = byte;
            return;

        case 2: // Transition from run
            stdLbm_countPtr = stdLbm_writePtr;
            if ( byte != stdLbm_lastByte )
            {
                // Start new literal
                *stdLbm_writePtr = 1;
                stdLbm_lastByte = byte;
                stdLbm_writePtr[1] = stdLbm_lastByte;
                stdLbm_state = 0;
                stdLbm_runCount = 0;
                stdLbm_writePtr[2] = byte;
                stdLbm_writePtr += 3;
                stdLbm_outputCount += 3;
                return;
            }
            // Continue run
            *stdLbm_writePtr = -1;
            stdLbm_state = 1;
            stdLbm_lastByte = byte;
            stdLbm_writePtr[1] = byte;
            stdLbm_writePtr += 2;
            stdLbm_outputCount += 2;
            return;

        case 3: // Flush - reset for next block
            stdLbm_lastByte = byte;
            stdLbm_state = 2;
            return;

        default:
            stdLbm_lastByte = byte;
            return;
    }
}

// stdLbm_Load and stdLbm_Write require extensive IFF chunk parsing
// with big-endian byte swapping. Ghidra decompilation available but
// needs dedicated implementation with build verification.
// Load: 1691 bytes, parses FORM/ILBM/BMHD/BODY/CMAP chunks
// Write: 1044 bytes, writes IFF ILBM with PackBits compression
