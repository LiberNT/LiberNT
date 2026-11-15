/*
 */

#include <ntoskrnl.h>

NTKERNELAPI
KIRQL
KxGetCurrentIrql(VOID)
{
    return KeGetCurrentIrql();
}

NTKERNELAPI
VOID
{
    KeLowerIrql(NewIrql);
}

NTKERNELAPI
KIRQL
{
    return KfRaiseIrql(NewIrql);
}

NTKERNELAPI
KIRQL
KxRaiseIrqlToDpcLevel(VOID)
{
    return KeRaiseIrqlToDpcLevel();
}

KIRQL
NTAPI
KeGetEffectiveIrql(VOID)
{

}
