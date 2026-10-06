/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 scheduler subnode topology
 */

#include <kmt_test.h>

VOID Test_KeArm64SubNodeSched(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static VOID Arm64SubNodeCheckCpu(PKSCHEDULER_SUBNODE Expected)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKSCHEDULER_SUBNODE SubNode;
    ULONG MyNumber = KeGetCurrentProcessorNumber();
    KAFFINITY Active = KeQueryActiveProcessors();

    ok(Prcb != NULL, "Prcb NULL\n");
    if (!Prcb)
        return;

    SubNode = (PKSCHEDULER_SUBNODE)Prcb->SchedulerSubNode;
    ok(SubNode != NULL, "cpu %lu SchedulerSubNode NULL\n", MyNumber);
    if (!SubNode)
        return;

    ok((ULONG_PTR)SubNode >= 0xFFFF000000000000ULL,
       "cpu %lu SchedulerSubNode=%p not a kernel address\n", MyNumber, SubNode);
    ok(SubNode == Expected,
       "cpu %lu subnode=%p != shared %p\n", MyNumber, SubNode, Expected);

    ok((SubNode->Affinity & ((KAFFINITY)1 << MyNumber)) != 0,
       "cpu %lu not in subnode Affinity=0x%I64x\n",
       MyNumber, (ULONGLONG)SubNode->Affinity);
    ok((SubNode->Affinity & Active) == Active,
       "subnode Affinity=0x%I64x missing active=0x%I64x\n",
       (ULONGLONG)SubNode->Affinity, (ULONGLONG)Active);
    ok_eq_uint(SubNode->SubNodeNumber, 0);
    ok_eq_uint(SubNode->ParentNodeNumber, 0);
    ok_eq_ulong(SubNode->Lowest, 0u);
    ok(SubNode->Highest < (ULONG)KeNumberProcessors,
       "Highest=%lu >= KeNumberProcessors=%lu\n",
       SubNode->Highest, (ULONG)KeNumberProcessors);

    ok((SubNode->IdleCpuSet & ~SubNode->Affinity) == 0,
       "IdleCpuSet=0x%I64x has bits outside Affinity=0x%I64x\n",
       (ULONGLONG)SubNode->IdleCpuSet, (ULONGLONG)SubNode->Affinity);
    ok((SubNode->IdleSmtSet & ~SubNode->Affinity) == 0,
       "IdleSmtSet=0x%I64x has bits outside Affinity=0x%I64x\n",
       (ULONGLONG)SubNode->IdleSmtSet, (ULONGLONG)SubNode->Affinity);
    ok((SubNode->SiblingMask & ~SubNode->Affinity) == 0,
       "SiblingMask=0x%I64x outside Affinity=0x%I64x\n",
       (ULONGLONG)SubNode->SiblingMask, (ULONGLONG)SubNode->Affinity);
}

static VOID Arm64SubNodeSched(VOID)
{
    ULONG Cpu;
    ULONG Processors = KeNumberProcessors;
    PKSCHEDULER_SUBNODE Shared;

    Shared = (PKSCHEDULER_SUBNODE)KeGetCurrentPrcb()->SchedulerSubNode;
    ok(Shared != NULL, "SchedulerSubNode NULL on the boot CPU\n");
    if (!Shared)
    {
        skip(FALSE, "Scheduler subnode topology not populated\n");
        return;
    }

    for (Cpu = 0; Cpu < Processors; Cpu++)
    {
        KeSetSystemAffinityThread((KAFFINITY)1 << Cpu);
        Arm64SubNodeCheckCpu(Shared);
    }
    KeRevertToUserAffinityThread();

    dump_trace("[arm64][KeArm64SubNodeSched] subnode=%p Affinity=0x%I64x IdleCpuSet=0x%I64x validated on %lu cpus\n",
               Shared, (ULONGLONG)Shared->Affinity,
               (ULONGLONG)Shared->IdleCpuSet, Processors);
}

#endif /* _M_ARM64 */

START_TEST(KeArm64SubNodeSched)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64SubNodeSched is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64SubNodeSched] enter\n");
    Arm64SubNodeSched();
#endif
}
