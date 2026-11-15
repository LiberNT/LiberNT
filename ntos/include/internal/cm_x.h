/*






}

FORCEINLINE
VOID
{
    ASSERT(CmpCacheTable[Index].Owner != KeGetCurrentThread());
    ExAcquirePushLockExclusive(&CmpCacheTable[Index].Lock);
    CmpCacheTable[Index].Owner = KeGetCurrentThread();
}

FORCEINLINE
VOID
{
}

FORCEINLINE
VOID
{
}

FORCEINLINE
BOOLEAN
{

        return FALSE;

    return TRUE;
}

FORCEINLINE
VOID
{
}

FORCEINLINE
VOID
{
}

FORCEINLINE
VOID
{
}

FORCEINLINE
VOID
{
}

FORCEINLINE
VOID
{
}
