#ifndef REIPAN_MOUSE_ACTIONS_H
#define REIPAN_MOUSE_ACTIONS_H

#include "sdk/scetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What the mouse's buttons do in play (io/reipan_game_mouse.h has the
 * controls themselves).  They press the game's own buttons, through the
 * button layout in force (pad_key), so the game runs exactly the code it runs
 * for the pad:
 *
 *   Raise the Camera Obscura   pad_key FINDER to raise, FINDER_OFF to lower.
 *                              Hold: up while held (a release during the
 *                              raise lowers it once it is up).  Toggle: a
 *                              click each way.
 *   Shutter                    pad_key 06 while held, the Camera Obscura up.
 *   Lens shot                  pad_key LENS_SHOT, the Camera Obscura up.
 *   Previous / next film       the d-pad's film change (GetDirTrg 0 / 1),
 *                              whichever control the options give it.
 *
 * Only in the game's play phases (Story_Normal and the photo after a shot),
 * and the raise only when the player could raise it from the pad.
 */

/* The FF2-internal pad bits the mouse holds down this read, OR'd into port
 * 0's held buttons by PadReadFunc before it takes the edges. */
u_short ReiPan_MouseActionPadBits(void);

/* A release stays pending while the photo or another lock blocks lowering. */
int ReiPan_MouseActionLowerRequested(void);

/* GetDirTrg()'s film change: the wheel stepped film `dir` (0 previous, 1 next)
 * this frame. */
int ReiPan_MouseActionFilmTrg(int dir);

#ifdef __cplusplus
}
#endif

#endif /* REIPAN_MOUSE_ACTIONS_H */
