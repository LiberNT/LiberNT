/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WSK name resolution, declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _KMTEST_WSKNAME_H_
#define _KMTEST_WSKNAME_H_

#define IOCTL_WSKNAME_RUN 1

/* The resolver in the user-mode part answers this name with this address. */
#define WSKNAME_TEST_NODE L"kmtest.invalid"
#define WSKNAME_TEST_ADDRESS 0x0A090807
#define WSKNAME_TEST_PORT 8081

#endif
