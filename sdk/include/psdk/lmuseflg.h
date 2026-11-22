#ifndef _LMUSEFLG_H
#define _LMUSEFLG_H

#define USE_NOFORCE 0
#define USE_FORCE 1
#define USE_LOTS_OF_FORCE 2

#define _LMUSEFLG_

#define FORCE_LEVEL(LEVELFLAGS) ((LEVELFLAGS) & 0xffff)

#endif
