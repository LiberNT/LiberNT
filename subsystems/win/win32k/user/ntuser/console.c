/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS Win32k subsystem
 * PURPOSE:          Console support functions for CONSRV
 * FILE:             win32ss/user/ntuser/console.c
 * PROGRAMMER:       Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 *                   Ahmed ARIF (arif.ing@outlook.com)
 */

#include <win32k.h>
DBG_DEFAULT_CHANNEL(UserMisc);

NTSTATUS
APIENTRY
NtUserConsoleControl(
    IN CONSOLECONTROL ConsoleCtrl,
    IN PVOID ConsoleCtrlInfo,
    IN ULONG ConsoleCtrlInfoLength)
{
    NTSTATUS Status = STATUS_SUCCESS;

    switch (ConsoleCtrl)
    {
        case ConsoleSetForeground:
        {
            CONSOLESETFOREGROUND SetForeground;
            PEPROCESS Process;

            if (ConsoleCtrlInfoLength != sizeof(SetForeground))
                return STATUS_INFO_LENGTH_MISMATCH;

            _SEH2_TRY
            {
                ProbeForRead(ConsoleCtrlInfo, ConsoleCtrlInfoLength, sizeof(HANDLE));
                SetForeground = *(PCONSOLESETFOREGROUND)ConsoleCtrlInfo;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;

            Status = ObReferenceObjectByHandle(SetForeground.hProcess,
                                               PROCESS_SET_INFORMATION,
                                               *PsProcessType,
                                               UserMode,
                                               (PVOID*)&Process,
                                               NULL);
            if (!NT_SUCCESS(Status))
                return Status;

            UserEnterExclusive();
            Status = IntSetConsoleForeground(Process, SetForeground.bForeground);
            UserLeave();

            ObDereferenceObject(Process);
            break;
        }

        default:
            ERR("Calling invalid control %d in NtUserConsoleControl\n", ConsoleCtrl);
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    return Status;
}

/* EOF */
