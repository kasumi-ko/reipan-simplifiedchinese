/* ==========================================================================
 *  ingame/map/MapRoomFile.h
 *
 *  A room's files (FF3-only, 0x1f9b48..0x1fa310). Each area has seven
 *  consecutive files from ROS00_PK4: the model PK4, the data PK4 and five
 *  obj_pack PHFs (one per side). MapLoad.c requests them into a room buffer
 *  (MAP_ROOM_DAT), then places the room by its area_pos entry and hands its
 *  hit data on. The RegList helpers keep a room buffer's 16 RegDat slot ids
 *  (-1 free).
 * ======================================================================== */
#ifndef _INGAME_MAP_MAPROOMFILE_H
#define _INGAME_MAP_MAPROOMFILE_H

#include "sdk/scetypes.h"

struct MAP_ROOM_DAT;                    // ingame/map/MapLoad.h

#define REG_LIST_NUM 16                 // RegDat slot ids per list

int    RoomFileGetMdlPk4(int area);     // FF3 0x001f9b48: the area's model PK4 file
int    RoomFileGetObjPack(int area);
u_char RoomFileGetSide(int area);       // FF3 0x001f9b98: its obj_pack side (areas 0x2f..0x31, 0x34 follow MapAreaSideGet)
void  *RoomFileMdlLoadReq(MAP_ROOM_DAT *dat, int area, void **addr);   // FF3 0x001f9c10: request 0; returns the load address
void   RoomFileSetHit(MAP_ROOM_DAT *dat);           // FF3 0x001f9c48
void  *RoomFileGetAreaPos(MAP_ROOM_DAT *dat);       // FF3 0x001f9c98: the side's area_pos data
void  *RoomFileLoadReq(int area, MAP_ROOM_DAT *dat);    // FF3 0x001f9ce0 (no callers): the address after the files
int    RoomFileIsLoadEnd(void);         // FF3 0x001f9d58 (no callers)
void   RoomFileLoadEnd(MAP_ROOM_DAT *dat);          // FF3 0x001f9d70 (no callers)
void  *RoomObjPackLoadReq(int area, MAP_ROOM_DAT *dat); // FF3 0x001f9de8: the address after the files
int    RoomObjPackIsLoadEnd(void);      // FF3 0x001f9e70
void  *RoomObjPackLoadEnd(MAP_ROOM_DAT *dat);       // FF3 0x001f9e88: the address after the furniture

int    RegListCheck(const s_char *list, int arg);   // FF3 0x001f9f18
void   RegListExe(const s_char *list);  // FF3 0x001f9f98
s_char RegListFindLabel(const s_char *list, int label);     // FF3 0x001f9ff8: the slot id, -1
void   RegListSetOffset(const s_char *list, float x, float y, float z);   // FF3 0x001fa090 (0x11b498: the room buffer's list at +0x1660)
int    RegListGetBuffID(const s_char *list, int floor, float *pos);  // FF3 0x001fa120: 1 when a slot holds pos on floor
void   RegListClear(s_char *list);      // FF3 0x001fa1b8
void   RegListRegist(void *pk4, const float *ofs, s_char *list);    // FF3 0x001fa1e8
void   RegListDelete(s_char *list);     // FF3 0x001fa2a0

#endif /* _INGAME_MAP_MAPROOMFILE_H */
