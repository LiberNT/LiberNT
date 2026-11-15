/*

FORCEINLINE
VOID
{

    {
    }
}

FORCEINLINE
VOID
{
    PS_CREATE_NOTIFY_INFO CreateInfo;


    if (Create)
    {
        RtlZeroMemory(&CreateInfo, sizeof(CreateInfo));
        CreateInfo.Size = sizeof(CreateInfo);
        CreateInfo.CreatingThreadId = PsGetCurrentThread()->Cid;
        CreateInfo.CreationStatus = STATUS_SUCCESS;
        {
            CreateInfo.FileOpenNameAvailable = TRUE;
        }
    }

    {

        else

    }
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
    KeEnterCriticalRegion();
}

FORCEINLINE
VOID
{
    KeLeaveCriticalRegion();
}

