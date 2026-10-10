/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Counting semaphore of the felix86 sources over an NT slim lock
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    void *Lock;
} sem_t;

int sem_init(sem_t *Semaphore, int Shared, unsigned int Value);
int sem_wait(sem_t *Semaphore);
int sem_post(sem_t *Semaphore);

#ifdef __cplusplus
}
#endif
