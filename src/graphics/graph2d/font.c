/* ==========================================================================
 *  graphics/graph2d/font.c
 *
 *  FF3-only (see font.h): the font texture packs. Boot phases 1 (Boot_Load)
 *  and PadCheck request the loads; the packs stay resident at their fixed
 *  EE addresses, and the text drawing paths send them to VRAM each time
 *  they need them (other 2D code reuses the same texture pages).
 * ======================================================================== */
#include "graphics/graph2d/font.h"

#include "graphics/graph2d/tim2.h"      // PK4SendVram
#include "reipan/reipan_memory.h"       // PC: ReiPan_GetHostPointer
#include "system/eeiop/cd_file_dat.h"   // FONT_TEX_PK4, FONT_EX_TEX_PK4
#include "system/os/eecdvd.h"           // LoadReq

// FF3 0x001c7c38.
void FontTexLoadReq(void)
{
    LoadReq(FONT_TEX_PK4, ReiPan_GetHostPointer(FONT_TEX_ADRS));
#ifndef REIPAN_SLPS_25544_CHS
    LoadReq(FONT_EX_TEX_PK4, ReiPan_GetHostPointer(FONT_EX_TEX_ADRS));
#endif
}

// FF3 0x001c7c68.
void FontTexSendVram(void)
{
    PK4SendVram(ReiPan_GetHostPointer(FONT_TEX_ADRS), 0, -1, -1);
}

// FF3 0x001c7c90.
void FontExTexSendVram(void)
{
#ifdef REIPAN_SLPS_25544_CHS
    FontTexSendVram(); // The Chinese pack contains all ten pages.
#else
    PK4SendVram(ReiPan_GetHostPointer(FONT_EX_TEX_ADRS), 0, -1, -1);
#endif
}
