/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     States an NDIS Poll object moves through between a request and the end of polling
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#undef NDEBUG
#include <assert.h>
#include <stdio.h>

typedef unsigned char UCHAR;
typedef unsigned long ULONG;
typedef long LONG;
typedef UCHAR BOOLEAN;
#define TRUE 1
#define FALSE 0
#define FORCEINLINE static inline
#define _In_
#define _Out_

#include "ndis6_poll.h"

static void
TestRequest(void)
{
    LONG State;

    assert(Ndis6PollCoreRequest(NDIS6_POLL_IDLE, &State) && State == NDIS6_POLL_QUEUED);
    assert(!Ndis6PollCoreRequest(NDIS6_POLL_QUEUED, &State) && State == NDIS6_POLL_QUEUED);
    assert(!Ndis6PollCoreRequest(NDIS6_POLL_RUNNING, &State) && State == NDIS6_POLL_RERUN);
    assert(!Ndis6PollCoreRequest(NDIS6_POLL_RERUN, &State) && State == NDIS6_POLL_RERUN);
}

static void
TestFinish(void)
{
    LONG State;

    assert(!Ndis6PollCoreFinish(NDIS6_POLL_RUNNING, &State) && State == NDIS6_POLL_IDLE);
    assert(Ndis6PollCoreFinish(NDIS6_POLL_RERUN, &State) && State == NDIS6_POLL_RUNNING);
}

static void
TestProgress(void)
{
    assert(!Ndis6PollCoreMadeProgress(0, 0));
    assert(Ndis6PollCoreMadeProgress(1, 0));
    assert(Ndis6PollCoreMadeProgress(0, 1));
    assert(Ndis6PollCoreMadeProgress(NDIS6_POLL_MAX_NBLS, NDIS6_POLL_MAX_NBLS));
}

static void
TestInterruptDuringPoll(void)
{
    LONG State = NDIS6_POLL_IDLE, Next;
    ULONG Queued = 0;

    if (Ndis6PollCoreRequest(State, &Next))
        Queued++;
    State = Next;
    assert(Queued == 1 && State == NDIS6_POLL_QUEUED);

    State = NDIS6_POLL_RUNNING;
    if (Ndis6PollCoreRequest(State, &Next))
        Queued++;
    State = Next;
    if (Ndis6PollCoreRequest(State, &Next))
        Queued++;
    State = Next;
    assert(Queued == 1 && State == NDIS6_POLL_RERUN);

    assert(Ndis6PollCoreFinish(State, &Next));
    State = Next;
    assert(State == NDIS6_POLL_RUNNING);
    assert(!Ndis6PollCoreFinish(State, &Next));
    State = Next;
    assert(State == NDIS6_POLL_IDLE);

    if (Ndis6PollCoreRequest(State, &Next))
        Queued++;
    assert(Queued == 2 && Next == NDIS6_POLL_QUEUED);
}

int
main(void)
{
    TestRequest();
    TestFinish();
    TestProgress();
    TestInterruptDuringPoll();
    printf("ndis poll: ok\n");
    return 0;
}
