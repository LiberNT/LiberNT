/*++ NDK Version: 0098

Copyright (c) Alex Ionescu.  All rights reserved.

Header Name:

    inbvtypes.h

Abstract:

    Type definitions for the Boot Video Driver.

Author:

    Alex Ionescu (alexi@tinykrnl.org) - Created - 02-Feb-2007

--*/

#ifndef _INBVTYPES_H
#define _INBVTYPES_H

//
// Dependencies
//
#include <umtypes.h>
#ifndef NTOS_MODE_USER

//
// Boot Video Display Ownership Status
//
typedef enum _INBV_DISPLAY_STATE
{
    INBV_DISPLAY_STATE_OWNED,
    INBV_DISPLAY_STATE_DISABLED,
    INBV_DISPLAY_STATE_LOST
} INBV_DISPLAY_STATE;

//
// Virtual frame buffer the boot video driver draws to instead of the firmware frame buffer
// FIXME: only the first two fields are known; the rest of the descriptor is INFO_NEEDED
//
typedef struct _INBV_VIRTUAL_FRAME_BUFFER
{
    PVOID Buffer;
    SIZE_T Size;
} INBV_VIRTUAL_FRAME_BUFFER, *PINBV_VIRTUAL_FRAME_BUFFER;

//
// Function Callbacks
//
typedef
BOOLEAN
(NTAPI *INBV_RESET_DISPLAY_PARAMETERS)(
    _In_ ULONG Cols,
    _In_ ULONG Rows
);

typedef
VOID
(NTAPI *INBV_DISPLAY_STRING_FILTER)(
    _Inout_ PCSTR* String
);

#endif
#endif
