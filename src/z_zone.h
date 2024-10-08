//
//  Copyright (C) 1999 by
//  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
// DESCRIPTION:
//      Zone Memory Allocation, perhaps NeXT ObjectiveC inspired.
//      Remark: this was the only stuff that, according
//       to John Carmack, might have been useful for
//       Quake.
//
// Rewritten by Lee Killough, though, since it was not efficient enough.
//
//---------------------------------------------------------------------

#ifndef __Z_ZONE__
#define __Z_ZONE__

#include <stddef.h>

// ZONE MEMORY
// PU - purge tags.

typedef enum {
  PU_STATIC,
  PU_LEVEL,
  PU_RENDERER,
  PU_VALLOC,
  PU_CACHE,
  /* Must always be last -- killough */
  PU_MAX
} pu_tag;

#define PU_LEVSPEC PU_LEVEL

void *Z_Malloc(size_t size, pu_tag tag, void **ptr);
void Z_Free(void *ptr);
void Z_FreeTag(pu_tag tag);
void Z_ChangeTag(void *ptr, pu_tag tag);
void *Z_Calloc(size_t n, size_t n2, pu_tag tag, void **user);
void *Z_Realloc(void *p, size_t n, pu_tag tag, void **user);

char *Z_StrDup(const char *orig, pu_tag tag);

#endif

//----------------------------------------------------------------------------
//
// $Log: z_zone.h,v $
// Revision 1.7  1998/05/08  20:32:12  killough
// fix __attribute__ redefinition
//
// Revision 1.6  1998/05/03  22:38:11  killough
// Remove unnecessary #include
//
// Revision 1.5  1998/04/27  01:49:42  killough
// Add history of malloc/free and scrambler (INSTRUMENTED only)
//
// Revision 1.4  1998/03/23  03:43:54  killough
// Make Z_CheckHeap() more diagnostic
//
// Revision 1.3  1998/02/02  13:28:06  killough
// Add dprintf
//
// Revision 1.2  1998/01/26  19:28:04  phares
// First rev with no ^Ms
//
// Revision 1.1.1.1  1998/01/19  14:03:06  rand
// Lee's Jan 19 sources
//
//
//----------------------------------------------------------------------------
