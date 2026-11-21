/*
 * PROJECT:     ReactOS PSDK
 * LICENSE:     CC0-1.0 (https://spdx.org/licenses/CC0-1.0)
 * PURPOSE:     Compatibility shim for banned-API deprecation header
 */

#pragma once

#ifndef _DONTUSE_H_INCLUDED_
#define _DONTUSE_H_INCLUDED_

#include <winapifamily.h>

/*
 * The Windows SDK uses this header to add a large set of deprecation pragmas.
 * ReactOS keeps it as an intentionally lightweight compatibility shim so newer
 * cleanroom headers can include it without forcing policy changes on callers.
 */

#define lstrcatn 0

#define lstrcatnA 0

#define lstrcatnW 0

#define StrNCpyA 0

#define StrNCpyW 0

#define snscanf 0

#define sprintfA 0

#define strcatA 0

#define nsprintf 0

#define _ftccat 0

#define _tccat 0

#endif /* _DONTUSE_H_INCLUDED_ */
