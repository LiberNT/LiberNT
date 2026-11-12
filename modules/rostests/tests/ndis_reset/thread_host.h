/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host synchronization for the NDIS reset source fixture
 * COPYRIGHT:   Copyright 2026 orhunnx <orhuntopcux@gmail.com>
 */

#pragma once

int ResetTestHostInitialize(void);
void ResetTestHostDestroy(void);
void ResetTestHostEnter(void);
void ResetTestHostLeave(void);
int ResetTestHostWait(unsigned Event);
void ResetTestHostSignal(unsigned Event);
int ResetTestHostRun(void (*Callback)(void));
int ResetTestHostJoin(void);
