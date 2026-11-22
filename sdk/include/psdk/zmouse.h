/*
       zmouse.h - Header for IntelliMouse.

       This file is part of a free library for the Win32 API.

       This library is distributed in the hope that it will be useful,
       but WITHOUT ANY WARRANTY; without even the implied warranty of
       MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

       FIXME: This file is obviously horribly incomplete!

*/

#ifndef _ZMOUSE_H
#define _ZMOUSE_H

#ifndef WM_MOUSEWHEEL
# define WM_MOUSEWHEEL (WM_MOUSELAST + 1)
#endif

#ifndef WHEEL_DELTA
# define WHEEL_DELTA 120
#endif

#ifndef WHEEL_PAGESCROLL
# define WHEEL_PAGESCROLL UINT_MAX
#endif

#ifndef SPI_SETWHEELSCROLLLINES
# define SPI_SETWHEELSCROLLLINES 105
#endif

#if (!(defined(UNICODE)))
#define MOUSEZ_CLASSNAME 0
#define MOUSEZ_TITLE "Magellan MSWHEEL"
#define MSH_MOUSEWHEEL 0
#define MSH_SCROLL_LINES "MSH_SCROLL_LINES_MSG"
#endif
#define MSH_WHEELMODULE_CLASS (MOUSEZ_CLASSNAME)
#define MSH_WHEELMODULE_TITLE (MOUSEZ_TITLE)
#if (!(defined(UNICODE)))
#define MSH_WHEELSUPPORT "MSH_WHEELSUPPORT_MSG"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
HWND HwndMSWheel(PUINT puiMsh_MsgMouseWheel, PUINT puiMsh_Msg3DSupport, PUINT puiMsh_MsgScrollLines, PBOOL pf3DSupport, PINT piScrollLines);

#ifdef __cplusplus
}
#endif

#endif /* _ZMOUSE_H */
