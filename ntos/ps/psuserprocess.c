/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Making a process and its first thread in one call, from the
 *              image the caller names
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>


/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Reads one attribute's value out of the caller's memory. An attribute that
 * is longer than a word points at its value rather than carrying it, and
 * that memory is the caller's, so it is copied before it is looked at.
 */
static
NTSTATUS
PspCaptureAttributeValue(
    _In_reads_bytes_(Size) PVOID Value,
    _In_ SIZE_T Size,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_writes_bytes_(Size) PVOID Captured)
{
    PAGED_CODE();

    if (Value == NULL) return STATUS_INVALID_PARAMETER;

    if (PreviousMode == KernelMode)
    {
        RtlCopyMemory(Captured, Value, Size);
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        ProbeForRead(Value, Size, sizeof(ULONG));
        RtlCopyMemory(Captured, Value, Size);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

/*
 * Reads the attribute list the caller passed. Every attribute is read once,
 * into the kernel's own memory where the caller cannot change it, and one
 * that is not understood makes the whole call fail rather than being
 * silently dropped: a caller that asked for something has to be told that it
 * was not done.
 */
NTSTATUS
NTAPI
PspCaptureAttributes(
    _In_opt_ PPS_ATTRIBUTE_LIST AttributeList,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PPSP_CREATE_ATTRIBUTES Attributes)
{
    PS_ATTRIBUTE Safe[PSP_MAX_ATTRIBUTES];
    SIZE_T TotalLength;
    NTSTATUS Status;
    ULONG Count;
    ULONG Index;

    PAGED_CODE();

    RtlZeroMemory(Attributes, sizeof(*Attributes));

    if (AttributeList == NULL) return STATUS_SUCCESS;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(AttributeList,
                         sizeof(SIZE_T),
                         sizeof(SIZE_T));
            TotalLength = AttributeList->TotalLength;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        TotalLength = AttributeList->TotalLength;
    }

    if (TotalLength < sizeof(SIZE_T)) return STATUS_INVALID_PARAMETER;

    TotalLength -= sizeof(SIZE_T);
    if ((TotalLength % sizeof(PS_ATTRIBUTE)) != 0)
        return STATUS_INVALID_PARAMETER;

    Count = (ULONG)(TotalLength / sizeof(PS_ATTRIBUTE));
    if (Count > PSP_MAX_ATTRIBUTES) return STATUS_INVALID_PARAMETER;
    if (Count == 0) return STATUS_SUCCESS;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(AttributeList->Attributes,
                         Count * sizeof(PS_ATTRIBUTE),
                         sizeof(ULONG_PTR));
            RtlCopyMemory(Safe,
                          AttributeList->Attributes,
                          Count * sizeof(PS_ATTRIBUTE));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(Safe,
                      AttributeList->Attributes,
                      Count * sizeof(PS_ATTRIBUTE));
    }

    for (Index = 0; Index < Count; Index++)
    {
        ULONG Number = (ULONG)(Safe[Index].Attribute & PSP_ATTRIBUTE_NUMBER_MASK);

        switch (Number)
        {
            case PsAttributeParentProcess:
                Attributes->ParentProcess = (HANDLE)Safe[Index].Value;
                break;

            case PsAttributeDebugPort:
                Attributes->DebugPort = (HANDLE)Safe[Index].Value;
                break;

            case PsAttributeToken:
                Attributes->Token = (HANDLE)Safe[Index].Value;
                break;

            case PsAttributeClientId:
                if (Safe[Index].Size != sizeof(CLIENT_ID))
                    return STATUS_INVALID_PARAMETER;
                Attributes->ClientId = Safe[Index].ValuePtr;
                break;

            case PsAttributeTebAddress:
                if (Safe[Index].Size != sizeof(PVOID))
                    return STATUS_INVALID_PARAMETER;
                Attributes->TebAddress = Safe[Index].ValuePtr;
                break;

            case PsAttributeImageName:
                Attributes->CapturedImageName.Buffer = Safe[Index].ValuePtr;
                Attributes->CapturedImageName.Length = (USHORT)Safe[Index].Size;
                Attributes->CapturedImageName.MaximumLength =
                    (USHORT)Safe[Index].Size;
                Attributes->ImageName = &Attributes->CapturedImageName;
                break;

            case PsAttributeImageInfo:
                if (Safe[Index].Size != sizeof(SECTION_IMAGE_INFORMATION))
                    return STATUS_INVALID_PARAMETER;
                Attributes->ImageInfo = Safe[Index].ValuePtr;
                break;

            case PsAttributeMemoryReserve:
                Attributes->MemoryReserve = (SIZE_T)Safe[Index].Value;
                break;

            case PsAttributePriorityClass:
                Attributes->PriorityClass = (ULONG)Safe[Index].Value;
                break;

            case PsAttributeErrorMode:
                Attributes->ErrorMode = (ULONG)Safe[Index].Value;
                break;

            case PsAttributeHandleList:
                if ((Safe[Index].Size % sizeof(HANDLE)) != 0)
                    return STATUS_INVALID_PARAMETER;
                Attributes->HandleList = Safe[Index].ValuePtr;
                Attributes->HandleCount = Safe[Index].Size / sizeof(HANDLE);
                break;

            case PsAttributeMachineType:
                Attributes->MachineType = (USHORT)Safe[Index].Value;
                break;

            case PsAttributeComponentFilter:
                Attributes->ComponentFilter = (ULONG)Safe[Index].Value;
                if ((Attributes->ComponentFilter &
                     ~PSP_COMPONENT_FILTER_VALID_FLAGS) != 0)
                {
                    return STATUS_INVALID_PARAMETER;
                }
                break;

            case PsAttributeGroupAffinity:
                if (Attributes->HasGroupAffinity) return STATUS_INVALID_PARAMETER;
                if (Safe[Index].Size != sizeof(GROUP_AFFINITY))
                    return STATUS_INVALID_PARAMETER;

                Status = PspCaptureAttributeValue(Safe[Index].ValuePtr,
                                                  sizeof(GROUP_AFFINITY),
                                                  PreviousMode,
                                                  &Attributes->GroupAffinity);
                if (!NT_SUCCESS(Status)) return Status;

                Attributes->HasGroupAffinity = TRUE;
                break;

            case PsAttributeIdealProcessor:
                if (Safe[Index].Size != sizeof(PROCESSOR_NUMBER))
                    return STATUS_INVALID_PARAMETER;

                Status = PspCaptureAttributeValue(Safe[Index].ValuePtr,
                                                  sizeof(PROCESSOR_NUMBER),
                                                  PreviousMode,
                                                  &Attributes->IdealProcessor);
                if (!NT_SUCCESS(Status)) return Status;

                Attributes->HasIdealProcessor = TRUE;
                break;

            case PsAttributePreferredNode:
                Attributes->PreferredNode = (USHORT)Safe[Index].Value;
                Attributes->HasPreferredNode = TRUE;
                break;

            case PsAttributeMitigationOptions:
                /*
                 * The mask may be given as one word or as a longer run of
                 * them; only the first word is defined, and a caller that
                 * sets anything beyond it is asking for something this
                 * system does not have.
                 */
                if (Safe[Index].Size < sizeof(ULONG64))
                {
                    Attributes->MitigationOptions =
                        (ULONGLONG)Safe[Index].Value;
                    break;
                }

                Status = PspCaptureAttributeValue(
                             Safe[Index].ValuePtr,
                             sizeof(ULONGLONG),
                             PreviousMode,
                             &Attributes->MitigationOptions);
                if (!NT_SUCCESS(Status)) return Status;
                break;

            case PsAttributeMitigationAuditOptions:
                if (Safe[Index].Size < sizeof(ULONG64))
                {
                    Attributes->MitigationAuditOptions =
                        (ULONGLONG)Safe[Index].Value;
                    break;
                }

                Status = PspCaptureAttributeValue(
                             Safe[Index].ValuePtr,
                             sizeof(ULONGLONG),
                             PreviousMode,
                             &Attributes->MitigationAuditOptions);
                if (!NT_SUCCESS(Status)) return Status;
                break;

            case PsAttributeProtectionLevel:
                if (Safe[Index].Size != sizeof(PS_PROTECTION))
                    return STATUS_INVALID_PARAMETER;

                Status = PspCaptureAttributeValue(Safe[Index].ValuePtr,
                                                  sizeof(PS_PROTECTION),
                                                  PreviousMode,
                                                  &Attributes->Protection);
                if (!NT_SUCCESS(Status)) return Status;

                if (Attributes->Protection.Type >= PsProtectedTypeMax ||
                    Attributes->Protection.Signer >= PsProtectedSignerMax)
                {
                    return STATUS_INVALID_PARAMETER;
                }

                Attributes->HasProtection = TRUE;
                break;

            case PsAttributeJobList:
                if (Safe[Index].Size == 0 ||
                    (Safe[Index].Size % sizeof(HANDLE)) != 0)
                {
                    return STATUS_INVALID_PARAMETER;
                }

                if (Safe[Index].Size / sizeof(HANDLE) > PSP_MAX_CREATE_JOBS)
                    return STATUS_INVALID_PARAMETER;

                Attributes->JobList = Safe[Index].ValuePtr;
                Attributes->JobCount =
                    (ULONG)(Safe[Index].Size / sizeof(HANDLE));
                break;

            case PsAttributeWin32kFilter:
                Attributes->Win32kFilter = (ULONG)Safe[Index].Value;
                break;

            case PsAttributeChildProcessPolicy:
                Attributes->ChildProcessPolicy = (ULONG)Safe[Index].Value;
                break;

            case PsAttributeAllApplicationPackagesPolicy:
                Attributes->AllApplicationPackagesPolicy =
                    (ULONG)Safe[Index].Value;
                break;

            case PsAttributeDesktopAppPolicy:
                Attributes->DesktopAppPolicy = (ULONG)Safe[Index].Value;
                break;

            case PsAttributeEnableOptionalXStateFeatures:
                Attributes->OptionalXStateFeatures =
                    (ULONGLONG)Safe[Index].Value;
                break;

            case PsAttributeChpe:
                Attributes->Chpe = (BOOLEAN)(Safe[Index].Value != 0);
                break;

            case PsAttributeStdHandleInfo:
            case PsAttributeSafeOpenPromptOriginClaim:
            case PsAttributeBnoIsolation:
                /*
                 * These three describe how the new process reaches things
                 * outside the kernel: its standard handles, where a prompt
                 * about an untrusted file came from, and which namespace it
                 * sees. Nothing in the kernel acts on them, and the
                 * subsystem that does reads them for itself, so they are
                 * accepted and passed over here.
                 */
                break;

            case PsAttributeSecureProcess:
            case PsAttributeUmsThread:
                /*
                 * One asks for a process inside a secure enclave and the
                 * other for user-mode scheduling. Neither exists in this
                 * system, and a caller that asked has to be told.
                 */
                return STATUS_NOT_SUPPORTED;

            default:
                return STATUS_INVALID_PARAMETER;
        }
    }

    return STATUS_SUCCESS;
}

/*
 * Puts the new thread where the caller asked for it to run. A thread that
 * was given neither a group nor an ideal processor runs wherever its
 * process runs, which is what the scheduler would pick anyway.
 */
NTSTATUS
NTAPI
PspApplyThreadAttributes(
    _In_ PETHREAD Thread,
    _In_ PPSP_CREATE_ATTRIBUTES Attributes)
{
    PAGED_CODE();

    if (Attributes->HasGroupAffinity)
    {
        if (Attributes->GroupAffinity.Group != 0) return STATUS_INVALID_PARAMETER;

        if (Attributes->GroupAffinity.Mask == 0 ||
            (Attributes->GroupAffinity.Mask &
             ~THREAD_TO_PROCESS(Thread)->Pcb.Affinity) != 0)
        {
            return STATUS_INVALID_PARAMETER;
        }

        KeSetAffinityThread(&Thread->Tcb, Attributes->GroupAffinity.Mask);
    }

    if (Attributes->HasIdealProcessor)
    {
        if (Attributes->IdealProcessor.Group != 0 ||
            Attributes->IdealProcessor.Number >= (UCHAR)KeNumberProcessors)
        {
            return STATUS_INVALID_PARAMETER;
        }

        KeSetIdealProcessorThread((PKTHREAD)&Thread->Tcb,
                                  Attributes->IdealProcessor.Number);
    }

    if (Attributes->HasPreferredNode)
    {
        /* TODO: start the thread's stack and its first pages on the node it
         * named, rather than wherever the memory manager would pick */
        NOTHING;
    }

    return STATUS_SUCCESS;
}

/*
 * Puts the new process under everything the caller asked for before any of
 * its code runs. Order matters: the mitigations and the protection have to
 * be in force before the first thread exists, and the jobs have to hold the
 * process before it can make a child that would escape them.
 */
static
NTSTATUS
PspApplyCreateAttributes(
    _In_ PEPROCESS Process,
    _In_ PPSP_CREATE_ATTRIBUTES Attributes,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    HANDLE Jobs[PSP_MAX_CREATE_JOBS];
    ULONG Policy;
    ULONG Index;
    NTSTATUS Status;

    PAGED_CODE();

    if (Attributes->MachineType != 0)
        Process->Machine = Attributes->MachineType;

    if (Attributes->Win32kFilter != 0)
        Process->Win32KFilterSet = Attributes->Win32kFilter;

    /*
     * The mitigation mask carries four bits for each policy, in the order
     * the policies are numbered, so each one is lifted out and set through
     * the same path a later caller would use.
     */
    if (Attributes->MitigationOptions != 0)
    {
        for (Policy = 0; Policy < PSP_EXTENDED_POLICY_COUNT; Policy++)
        {
            ULONG Flags;

            if (!PspIsExtendedMitigationPolicy(Policy)) continue;

            Flags = (ULONG)((Attributes->MitigationOptions >>
                             (Policy * 4)) & 0xF);
            if (Flags == 0) continue;

            PspLockMitigationPolicy();
            PspStoreMitigationPolicy(Process, Policy, Flags);
            PspUnlockMitigationPolicy();
        }
    }

    if (Attributes->MitigationAuditOptions != 0)
    {
        /* TODO: the audit half of each mitigation, which records what a
         * mitigation would have stopped without stopping it */
        NOTHING;
    }

    if (Attributes->HasProtection)
    {
        /*
         * A protected process is closed to everything that is not at least
         * as trusted, so only something trusted may ask for one.
         */
        if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
            return STATUS_PRIVILEGE_NOT_HELD;

        Process->Protection = Attributes->Protection;
    }

    if (Attributes->OptionalXStateFeatures != 0)
    {
        /* TODO: let the process use the extra processor state it asked for */
        NOTHING;
    }

    /*
     * A caller may ask for a run of the new process's address space to be
     * kept free of anything the loader might put there, which is how a
     * process that needs a fixed region gets one.
     */
    if (Attributes->MemoryReserve != 0)
    {
        SIZE_T Size = Attributes->MemoryReserve;
        PVOID Base = NULL;
        HANDLE Handle;

        Status = ObOpenObjectByPointer(Process,
                                       OBJ_KERNEL_HANDLE,
                                       NULL,
                                       PROCESS_VM_OPERATION,
                                       PsProcessType,
                                       KernelMode,
                                       &Handle);
        if (!NT_SUCCESS(Status)) return Status;

        Status = ZwAllocateVirtualMemory(Handle,
                                         &Base,
                                         0,
                                         &Size,
                                         MEM_RESERVE,
                                         PAGE_READWRITE);
        ZwClose(Handle);

        if (!NT_SUCCESS(Status)) return Status;
    }

    if (Attributes->Chpe)
    {
        /* TODO: map the hybrid native image for a process that asked for it */
        NOTHING;
    }

    if (Attributes->ChildProcessPolicy != 0 ||
        Attributes->AllApplicationPackagesPolicy != 0 ||
        Attributes->DesktopAppPolicy != 0)
    {
        /* TODO: these three bound what the process may start and which
         * namespace it sees; nothing in the kernel reads them yet */
        NOTHING;
    }

    if (Attributes->JobCount == 0) return STATUS_SUCCESS;

    /*
     * The jobs are read out of the caller's memory once, because a caller
     * that could change the list while it was being walked could put the
     * process into a job the check was not made against.
     */
    if (PreviousMode == KernelMode)
    {
        RtlCopyMemory(Jobs,
                      Attributes->JobList,
                      Attributes->JobCount * sizeof(HANDLE));
    }
    else
    {
        _SEH2_TRY
        {
            ProbeForRead(Attributes->JobList,
                         Attributes->JobCount * sizeof(HANDLE),
                         sizeof(HANDLE));
            RtlCopyMemory(Jobs,
                          Attributes->JobList,
                          Attributes->JobCount * sizeof(HANDLE));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /*
     * A process belongs to one job, so the innermost of the jobs named is
     * the one it joins: joining that one puts it under every job above it.
     */
    for (Index = 0; Index < Attributes->JobCount; Index++)
    {
        PEJOB Job;

        Status = ObReferenceObjectByHandle(Jobs[Index],
                                           JOB_OBJECT_ASSIGN_PROCESS,
                                           PsJobType,
                                           PreviousMode,
                                           (PVOID*)&Job,
                                           NULL);
        if (!NT_SUCCESS(Status)) return Status;

        Status = PspAssignProcessToJob(Process, Job);
        ObDereferenceObject(Job);

        if (!NT_SUCCESS(Status)) return Status;

        /* The first one that takes the process is the one it is in */
        break;
    }

    return STATUS_SUCCESS;
}

/*
 * Opens the image and makes a section of it. Which step failed is reported
 * back in the creation state, because a caller tells a missing file from a
 * file that is not an image by that and nothing else.
 */
static
NTSTATUS
PspOpenImageSection(
    _In_ PUNICODE_STRING ImageName,
    _In_ ACCESS_MASK AdditionalFileAccess,
    _Out_ PHANDLE FileHandle,
    _Outptr_ PVOID *Section,
    _Out_ PS_CREATE_STATE *FailState)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    *FileHandle = NULL;
    *Section = NULL;
    *FailState = PsCreateInitialState;

    InitializeObjectAttributes(&ObjectAttributes,
                               ImageName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenFile(&Handle,
                        SYNCHRONIZE | FILE_EXECUTE | FILE_READ_ATTRIBUTES |
                            AdditionalFileAccess,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_DELETE,
                        FILE_NON_DIRECTORY_FILE);
    if (!NT_SUCCESS(Status))
    {
        *FailState = PsCreateFailOnFileOpen;
        return Status;
    }

    Status = MmCreateSection(Section,
                             SECTION_ALL_ACCESS,
                             NULL,
                             NULL,
                             PAGE_EXECUTE,
                             SEC_IMAGE,
                             Handle,
                             NULL);
    if (!NT_SUCCESS(Status))
    {
        /*
         * A file that is there but is not an image of this machine is told
         * apart from one that could not be read at all, because the caller
         * acts differently on each.
         */
        *FailState = (Status == STATUS_INVALID_IMAGE_FORMAT ||
                      Status == STATUS_INVALID_IMAGE_NOT_MZ ||
                      Status == STATUS_INVALID_IMAGE_PROTECT)
                         ? PsCreateFailExeFormat
                         : PsCreateFailOnSectionCreate;

        ZwClose(Handle);
        return Status;
    }

    *FileHandle = Handle;
    return STATUS_SUCCESS;
}

/*
 * Writes the process parameters into the new process, so that the loader
 * finds its command line and environment where the block says they are.
 */
static
NTSTATUS
PspWriteProcessParameters(
    _In_ PEPROCESS Process,
    _In_ PRTL_USER_PROCESS_PARAMETERS Parameters,
    _Out_ PVOID *Remote)
{
    KAPC_STATE ApcState;
    SIZE_T Length;
    PVOID Base = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    *Remote = NULL;

    if (Parameters == NULL) return STATUS_SUCCESS;

    Length = Parameters->Length;
    if (Length < sizeof(RTL_USER_PROCESS_PARAMETERS))
        return STATUS_INVALID_PARAMETER;

    KeStackAttachProcess(&Process->Pcb, &ApcState);

    Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                     &Base,
                                     0,
                                     &Length,
                                     MEM_COMMIT | MEM_RESERVE,
                                     PAGE_READWRITE);
    if (NT_SUCCESS(Status))
    {
        _SEH2_TRY
        {
            RtlCopyMemory(Base, Parameters, Parameters->Length);
            ((PRTL_USER_PROCESS_PARAMETERS)Base)->MaximumLength =
                (ULONG)Length;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    KeUnstackDetachProcess(&ApcState);

    if (!NT_SUCCESS(Status)) return Status;

    *Remote = Base;

    if (Process->Peb != NULL)
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);

        _SEH2_TRY
        {
            Process->Peb->ProcessParameters = Base;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        KeUnstackDetachProcess(&ApcState);
    }

    return Status;
}

/* SYSTEM CALLS ***************************************************************/

/*
 * Makes a process and the thread that will run it, from the image the
 * attribute list names. The creation record the caller passes is both the
 * input and the answer: it says what sort of creation this is going in, and
 * on the way out it says how far the creation got and what it produced.
 */
NTSTATUS
NTAPI
NtCreateUserProcess(
    _Out_ PHANDLE ProcessHandle,
    _Out_ PHANDLE ThreadHandle,
    _In_ ACCESS_MASK ProcessDesiredAccess,
    _In_ ACCESS_MASK ThreadDesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ProcessObjectAttributes,
    _In_opt_ POBJECT_ATTRIBUTES ThreadObjectAttributes,
    _In_ ULONG ProcessFlags,
    _In_ ULONG ThreadFlags,
    _In_opt_ PRTL_USER_PROCESS_PARAMETERS ProcessParameters,
    _Inout_ PPS_CREATE_INFO CreateInfo,
    _Inout_opt_ PPS_ATTRIBUTE_LIST AttributeList)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PSP_CREATE_ATTRIBUTES Attributes;
    PS_CREATE_INFO Info;
    PS_CREATE_STATE FailState;
    INITIAL_TEB InitialTeb;
    CONTEXT Context;
    HANDLE NewProcess = NULL;
    HANDLE NewThread = NULL;
    HANDLE FileHandle = NULL;
    HANDLE SectionHandle = NULL;
    PVOID Section = NULL;
    PVOID RemoteParameters = NULL;
    PEPROCESS Process = NULL;
    SECTION_IMAGE_INFORMATION ImageInformation;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(ProcessHandle);
            ProbeForWriteHandle(ThreadHandle);
            ProbeForWrite(CreateInfo, sizeof(PS_CREATE_INFO), sizeof(ULONG));
            Info = *CreateInfo;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        Info = *CreateInfo;
    }

    if (Info.Size != sizeof(PS_CREATE_INFO)) return STATUS_INVALID_PARAMETER;
    if (Info.State != PsCreateInitialState) return STATUS_INVALID_PARAMETER;

    if ((ProcessFlags & ~PROCESS_CREATE_FLAGS_LEGAL_MASK) != 0)
        return STATUS_INVALID_PARAMETER;

    Status = PspCaptureAttributes(AttributeList, PreviousMode, &Attributes);
    if (!NT_SUCCESS(Status)) return Status;

    if (Attributes.ImageName == NULL) return STATUS_INVALID_PARAMETER;

    Status = PspOpenImageSection(Attributes.ImageName,
                                 Info.InitState.AdditionalFileAccess,
                                 &FileHandle,
                                 &Section,
                                 &FailState);
    if (!NT_SUCCESS(Status))
    {
        /*
         * The caller is told which step failed, which is the only way it can
         * tell a missing image from one it cannot run.
         */
        Info.State = FailState;
        if (FailState == PsCreateFailExeFormat) Info.ExeFormat.DllCharacteristics = 0;

        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                *CreateInfo = Info;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                /* The status already says the creation failed */
            }
            _SEH2_END;
        }
        else
        {
            *CreateInfo = Info;
        }

        return Status;
    }

    Status = MmGetSectionImageInformation(Section, &ImageInformation);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Section);
        ZwClose(FileHandle);
        return Status;
    }

    Status = ObOpenObjectByPointer(Section,
                                   OBJ_KERNEL_HANDLE,
                                   NULL,
                                   SECTION_ALL_ACCESS,
                                   MmSectionObjectType,
                                   KernelMode,
                                   &SectionHandle);
    ObDereferenceObject(Section);

    if (!NT_SUCCESS(Status))
    {
        ZwClose(FileHandle);
        return Status;
    }

    Status = PspCreateProcess(&NewProcess,
                              ProcessDesiredAccess,
                              ProcessObjectAttributes,
                              (Attributes.ParentProcess != NULL)
                                  ? Attributes.ParentProcess
                                  : NtCurrentProcess(),
                              ProcessFlags,
                              SectionHandle,
                              Attributes.DebugPort,
                              NULL,
                              FALSE,
                              Attributes.HandleList,
                              Attributes.HandleCount);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(SectionHandle);
        ZwClose(FileHandle);
        return Status;
    }

    Status = ObReferenceObjectByHandle(NewProcess,
                                       PROCESS_ALL_ACCESS,
                                       PsProcessType,
                                       PreviousMode,
                                       (PVOID*)&Process,
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        if (Attributes.Token != NULL)
        {
            Status = PspSetPrimaryToken(Process,
                                        Attributes.Token,
                                        NULL,
                                        FALSE);
        }

        if (NT_SUCCESS(Status) && Attributes.PriorityClass != 0)
        {
            PspSetProcessPriorityClass(Process,
                                       (UCHAR)Attributes.PriorityClass);
        }

        if (NT_SUCCESS(Status) && Attributes.ErrorMode != 0)
        {
            Process->DefaultHardErrorProcessing = Attributes.ErrorMode;
        }

        if (NT_SUCCESS(Status))
            Status = PspApplyCreateAttributes(Process,
                                              &Attributes,
                                              PreviousMode);

        if (NT_SUCCESS(Status))
        {
            Status = PspWriteProcessParameters(Process,
                                               ProcessParameters,
                                               &RemoteParameters);
        }
    }

    /*
     * The thread is made last, because it is what starts the process
     * running: nothing of the process may still need changing by then.
     */
    if (NT_SUCCESS(Status))
    {
        Status = PspAllocateUserStack(
                     NewProcess,
                     0,
                     (ImageInformation.MaximumStackSize != 0)
                         ? ImageInformation.MaximumStackSize
                         : PSP_DEFAULT_STACK_RESERVE,
                     ImageInformation.CommittedStackSize,
                     0,
                     &InitialTeb);
    }

    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Context, sizeof(Context));
        PsArchInitializeUserThreadContext(&Context,
                                          PspRtlUserThreadStart,
                                          ImageInformation.TransferAddress,
                                          NULL,
                                          InitialTeb.StackBase);

        Status = PspCreateThread(&NewThread,
                                 ThreadDesiredAccess,
                                 ThreadObjectAttributes,
                                 NewProcess,
                                 NULL,
                                 Attributes.ClientId,
                                 &Context,
                                 &InitialTeb,
                                 NULL,
                                 ThreadFlags |
                                     THREAD_CREATE_FLAGS_INITIAL_THREAD,
                                 NULL,
                                 NULL,
                                 ImageInformation.TransferAddress);
    }

    /*
     * Where the thread's own block landed, and what the image asked for, are
     * written back only once the thread exists: before that there is no
     * block, and a caller that was told of one would read nothing.
     */
    if (NT_SUCCESS(Status))
    {
        PETHREAD Created;

        Status = ObReferenceObjectByHandle(NewThread,
                                           0,
                                           PsThreadType,
                                           KernelMode,
                                           (PVOID*)&Created,
                                           NULL);
        if (NT_SUCCESS(Status))
        {
            PVOID Teb = Created->Tcb.Teb;

            Status = PspApplyThreadAttributes(Created, &Attributes);
            ObDereferenceObject(Created);

            if (!NT_SUCCESS(Status) || Attributes.TebAddress == NULL)
            {
                /* Nothing to write back, or nothing worked */
                NOTHING;
            }
            else if (PreviousMode != KernelMode)
            {
                _SEH2_TRY
                {
                    *Attributes.TebAddress = Teb;
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
            }
            else
            {
                *Attributes.TebAddress = Teb;
            }
        }
    }

    if (NT_SUCCESS(Status) && Attributes.ImageInfo != NULL)
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                *Attributes.ImageInfo = ImageInformation;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
        else
        {
            *Attributes.ImageInfo = ImageInformation;
        }
    }

    if (!NT_SUCCESS(Status))
    {
        if (Process != NULL)
        {
            PspTerminateProcess(Process, Status);
            ObDereferenceObject(Process);
        }

        ZwClose(NewProcess);
        ZwClose(SectionHandle);
        ZwClose(FileHandle);
        return Status;
    }

    /*
     * The creation worked, so the caller is handed what it needs to go on
     * with: the two handles, and in the record the file and the section it
     * ended up with along with where the process's own blocks landed.
     */
    Info.State = PsCreateSuccess;
    Info.SuccessState.OutputFlags = 0;
    Info.SuccessState.FileHandle = FileHandle;
    Info.SuccessState.SectionHandle = SectionHandle;
    Info.SuccessState.UserProcessParametersNative =
        (ULONGLONG)(ULONG_PTR)RemoteParameters;
    Info.SuccessState.UserProcessParametersWow64 = 0;
    Info.SuccessState.CurrentParameterFlags =
        (ProcessParameters != NULL) ? ProcessParameters->Flags : 0;
    Info.SuccessState.PebAddressNative =
        (ULONGLONG)(ULONG_PTR)Process->Peb;
    Info.SuccessState.PebAddressWow64 =
        (ULONG)(ULONG_PTR)PsGetProcessPeb32(Process);
    Info.SuccessState.ManifestAddress = 0;
    Info.SuccessState.ManifestSize = 0;

    if (PsIsProtectedProcess(Process))
    {
        Info.SuccessState.ProtectedProcess = 1;
        if (PsIsProtectedProcessLight(Process))
            Info.SuccessState.ProtectedProcessLight = 1;
    }

    ObDereferenceObject(Process);

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *CreateInfo = Info;
            *ProcessHandle = NewProcess;
            *ThreadHandle = NewThread;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *CreateInfo = Info;
        *ProcessHandle = NewProcess;
        *ThreadHandle = NewThread;
    }

    return Status;
}
