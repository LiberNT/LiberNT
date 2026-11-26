#ifndef _APITEST_IATHOOK_H
#define _APITEST_IATHOOK_H

static PIMAGE_IMPORT_DESCRIPTOR FindImportDescriptor(PBYTE DllBase, PCSTR DllName)
{
    ULONG Size;
    PIMAGE_IMPORT_DESCRIPTOR ImportDescriptor = RtlImageDirectoryEntryToData((HMODULE)DllBase, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &Size);
    if (!ImportDescriptor)
        return NULL;
    while (ImportDescriptor->Name && ImportDescriptor->OriginalFirstThunk)
    {
        PCHAR Name = (PCHAR)(DllBase + ImportDescriptor->Name);
        if (!lstrcmpiA(Name, DllName))
        {
            return ImportDescriptor;
        }
        ImportDescriptor++;
    }
    return NULL;
}

static PIMAGE_DELAYLOAD_DESCRIPTOR FindDelayImportDescriptor(PBYTE DllBase, PCSTR DllName)
{
    ULONG Size;
    PIMAGE_DELAYLOAD_DESCRIPTOR DelayDescriptor = RtlImageDirectoryEntryToData((HMODULE)DllBase, TRUE, IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT, &Size);
    if (!DelayDescriptor)
        return NULL;
    while (DelayDescriptor->DllNameRVA && DelayDescriptor->ImportNameTableRVA && DelayDescriptor->ImportAddressTableRVA)
    {
        PCHAR Name = DelayDescriptor->Attributes.RvaBased
            ? (PCHAR)(DllBase + DelayDescriptor->DllNameRVA)
            : (PCHAR)(ULONG_PTR)DelayDescriptor->DllNameRVA;
        if (!lstrcmpiA(Name, DllName))
        {
            return DelayDescriptor;
        }
        DelayDescriptor++;
    }
    return NULL;
}

static BOOL RedirectIatThunk(PBYTE NameBase, PIMAGE_THUNK_DATA OriginalThunk, PIMAGE_THUNK_DATA FirstThunk, PCSTR FunctionName, ULONG_PTR NewFunction, ULONG_PTR* OriginalFunction)
{
    while (OriginalThunk->u1.AddressOfData && FirstThunk->u1.Function)
    {
        if (!IMAGE_SNAP_BY_ORDINAL(OriginalThunk->u1.AddressOfData))
        {
            PIMAGE_IMPORT_BY_NAME ImportName = (PIMAGE_IMPORT_BY_NAME)(NameBase + OriginalThunk->u1.AddressOfData);
            if (!lstrcmpiA((PCSTR)ImportName->Name, FunctionName))
            {
                DWORD dwOld;
                VirtualProtect(&FirstThunk->u1.Function, sizeof(ULONG_PTR), PAGE_EXECUTE_READWRITE, &dwOld);
                *OriginalFunction = FirstThunk->u1.Function;
                FirstThunk->u1.Function = NewFunction;
                VirtualProtect(&FirstThunk->u1.Function, sizeof(ULONG_PTR), dwOld, &dwOld);
                return TRUE;
            }
        }
        OriginalThunk++;
        FirstThunk++;
    }
    return FALSE;
}

static BOOL RedirectIat(HMODULE TargetDll, PCSTR DllName, PCSTR FunctionName, ULONG_PTR NewFunction, ULONG_PTR* OriginalFunction)
{
    PBYTE DllBase = (PBYTE)TargetDll;
    PIMAGE_IMPORT_DESCRIPTOR ImportDescriptor = FindImportDescriptor(DllBase, DllName);
    if (ImportDescriptor)
    {
        // On loaded images, OriginalFirstThunk points to the name / ordinal of the function
        PIMAGE_THUNK_DATA OriginalThunk = (PIMAGE_THUNK_DATA)(DllBase + ImportDescriptor->OriginalFirstThunk);
        // FirstThunk points to the resolved address.
        PIMAGE_THUNK_DATA FirstThunk = (PIMAGE_THUNK_DATA)(DllBase + ImportDescriptor->FirstThunk);
        if (RedirectIatThunk(DllBase, OriginalThunk, FirstThunk, FunctionName, NewFunction, OriginalFunction))
            return TRUE;
        skip("Unable to find the Import %s!%s\n", DllName, FunctionName);
    }
    else
    {
        PIMAGE_DELAYLOAD_DESCRIPTOR DelayDescriptor = FindDelayImportDescriptor(DllBase, DllName);
        if (DelayDescriptor)
        {
            PBYTE NameBase = DelayDescriptor->Attributes.RvaBased ? DllBase : NULL;
            PIMAGE_THUNK_DATA OriginalThunk = (PIMAGE_THUNK_DATA)(NameBase + DelayDescriptor->ImportNameTableRVA);
            PIMAGE_THUNK_DATA FirstThunk = (PIMAGE_THUNK_DATA)(NameBase + DelayDescriptor->ImportAddressTableRVA);
            if (RedirectIatThunk(NameBase, OriginalThunk, FirstThunk, FunctionName, NewFunction, OriginalFunction))
                return TRUE;
            skip("Unable to find the DelayImport %s!%s\n", DllName, FunctionName);
        }
        else
        {
            skip("Unable to find the ImportDescriptor for %s\n", DllName);
        }
    }
    return FALSE;
}

static BOOL RestoreIat(HMODULE TargetDll, PCSTR DllName, PCSTR FunctionName, ULONG_PTR OriginalFunction)
{
    ULONG_PTR old = 0;
    return RedirectIat(TargetDll, DllName, FunctionName, OriginalFunction, &old);
}

 #endif // _APITEST_IATHOOK_H
 
