/*
 * PROJECT:     LiberNT mbed TLS port
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Mutex type of the Windows threading backend
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct mbedtls_threading_mutex_t
{
    void *MBEDTLS_PRIVATE(lock);
} mbedtls_threading_mutex_t;
