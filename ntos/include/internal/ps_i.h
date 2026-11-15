/*
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright Thomas Weidenmueller <w3seek@reactos.org>
 *              Copyright 2020-2021 George Bișoc <george.bisoc@reactos.org>
 */

#include "icif.h"

#define PSP_PAGE_PRIORITY_MINIMUM 1
#define PSP_PAGE_PRIORITY_NORMAL  5

typedef struct _PSP_MITIGATION_POLICY_INFORMATION
{
    ULONG Policy;
    ULONG Flags;
} PSP_MITIGATION_POLICY_INFORMATION;

static const INFORMATION_CLASS_INFO PsProcessInfoClass[] =
{
};

static const INFORMATION_CLASS_INFO PsThreadInfoClass[] =
{
#if defined(_X86_)
#else
#endif
};
