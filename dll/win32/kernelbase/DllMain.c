
#include <windows.h>
#include <winternl.h>

extern BOOL is_wow64;

void init_locale( HMODULE module );
void WerCleanupRuntimeExceptionModules(void);

const WCHAR windows_dir[] = L"C:\\windows";
const WCHAR system_dir[] = L"C:\\windows\\system32";

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            ULONG_PTR Wow64 = 0;

            if (NT_SUCCESS(NtQueryInformationProcess(NtCurrentProcess(), ProcessWow64Information,
                                                     &Wow64, sizeof(Wow64), NULL)))
                is_wow64 = Wow64 != 0;
            init_locale(hinstDLL);
            break;
        }

        case DLL_THREAD_ATTACH:
            break;

        case DLL_THREAD_DETACH:
            break;

        case DLL_PROCESS_DETACH:
            if (!lpvReserved) WerCleanupRuntimeExceptionModules();
            break;
    }

    return TRUE;
}
