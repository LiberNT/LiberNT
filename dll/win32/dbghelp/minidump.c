/*
 * File minidump.c - management of dumps (read & write)
 *
 * Copyright (C) 2004-2005, Eric Pouech
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <time.h>
#include <intrin.h>

#include "ntstatus.h"
#include "dbghelp_private.h"
#include "winternl.h"
#include "psapi.h"
#include "processsnapshot.h"
#include "ddk/wdm.h"
#include "wine/asm.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dbghelp);

static BOOL dump_read_process_memory(void *user, ULONG64 address, void *buffer, SIZE_T size)
{
    struct dump_context *dc = user;
    SIZE_T total = 0;
    DWORD error = ERROR_PARTIAL_COPY;

    if (address > ~(ULONG64)0 - size)
    {
        SetLastError(ERROR_INVALID_ADDRESS);
        return FALSE;
    }
    while (total < size)
    {
        MINIDUMP_CALLBACK_INPUT input = {0};
        MINIDUMP_CALLBACK_OUTPUT output = {0};
        ULONG requested = min(size - total, MAXDWORD), completed = 0;
        HRESULT status = S_OK;
        SIZE_T read = 0;

        input.ProcessHandle = dc->callback_handle;
        if (dc->callback_vm)
        {
            input.CallbackType = VmPreReadCallback;
            input.VmPreRead.Offset = address + total;
            input.VmPreRead.Buffer = (BYTE *)buffer + total;
            input.VmPreRead.Size = requested;
            output.VmReadStatus = E_NOTIMPL;
            if (dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output))
            {
                status = output.VmReadStatus;
                completed = min(output.VmReadBytesCompleted, requested);
                if (completed < requested)
                {
                    if (status == E_NOTIMPL) status = S_OK;
                    else if (FAILED(status)) break;
                }
            }
        }
        if (completed < requested && SUCCEEDED(status))
        {
            status = S_OK;
            if (address + total + completed != (ULONG_PTR)(address + total + completed))
            {
                error = ERROR_INVALID_ADDRESS;
                status = HRESULT_FROM_WIN32(error);
            }
            else
            {
                SIZE_T length = min(requested - completed,
                    sysinfo.dwPageSize - (address + total + completed) % sysinfo.dwPageSize);

                if (!ReadProcessMemory(dc->process->handle, (void *)(ULONG_PTR)(address + total + completed),
                                       (BYTE *)buffer + total + completed, length, &read))
                {
                    error = GetLastError();
                    status = HRESULT_FROM_WIN32(error);
                }
            }
            completed += min(read, requested - completed);
            if (dc->callback_vm)
            {
                input.CallbackType = VmPostReadCallback;
                input.VmPostRead.Offset = address + total;
                input.VmPostRead.Buffer = (BYTE *)buffer + total;
                input.VmPostRead.Size = requested;
                input.VmPostRead.Completed = completed;
                input.VmPostRead.Status = status;
                memset(&output, 0, sizeof(output));
                output.VmReadStatus = E_NOTIMPL;
                if (dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output))
                {
                    status = output.VmReadStatus;
                    completed = min(output.VmReadBytesCompleted, requested);
                    error = ERROR_PARTIAL_COPY;
                }
            }
        }
        total += completed;
        if (total == size) return TRUE;
        if (FAILED(status) || !completed) break;
    }
    if (total == size) return TRUE;
    SetLastError(total ? ERROR_READ_FAULT : error);
    return FALSE;
}

static BOOL dump_ignore_memory_failure(struct dump_context *dc, ULONG64 address, ULONG size)
{
    MINIDUMP_CALLBACK_INPUT input = {0};
    MINIDUMP_CALLBACK_OUTPUT output = {0};
    HRESULT status = HRESULT_FROM_WIN32(GetLastError());

    input.ProcessId = dc->pid;
    input.ProcessHandle = dc->callback_handle;
    input.CallbackType = ReadMemoryFailureCallback;
    input.ReadMemoryFailure.Offset = address;
    input.ReadMemoryFailure.Bytes = size;
    input.ReadMemoryFailure.FailureStatus = status;
    output.Status = S_FALSE;
    if (dc->cb && dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output) && output.Status == S_OK)
        return TRUE;
    dc->status = status;
    return FALSE;
}

static BOOL dump_query_memory_status(struct dump_context *dc, ULONG64 address,
                                     MINIDUMP_MEMORY_INFO *info, HRESULT *status)
{
    MEMORY_BASIC_INFORMATION mbi;

    if (status) *status = S_OK;

    if (dc->callback_vm)
    {
        MINIDUMP_CALLBACK_INPUT input = {0};
        MINIDUMP_CALLBACK_OUTPUT output = {0};

        input.ProcessHandle = dc->callback_handle;
        input.CallbackType = VmQueryCallback;
        input.VmQuery.Offset = address;
        output.VmQueryStatus = E_NOTIMPL;
        if (dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output) &&
            output.VmQueryStatus != E_NOTIMPL)
        {
            if (output.VmQueryStatus != S_OK)
            {
                if (status) *status = output.VmQueryStatus;
                return FALSE;
            }
            *info = output.VmQueryResult;
            return TRUE;
        }
    }
    if (address != (ULONG_PTR)address ||
        !VirtualQueryEx(dc->process->handle, (void *)(ULONG_PTR)address, &mbi, sizeof(mbi))) return FALSE;
    memset(info, 0, sizeof(*info));
    info->BaseAddress = (ULONG_PTR)mbi.BaseAddress;
    info->AllocationBase = (ULONG_PTR)mbi.AllocationBase;
    info->AllocationProtect = mbi.AllocationProtect;
    info->RegionSize = mbi.RegionSize;
    info->State = mbi.State;
    info->Protect = mbi.Protect;
    info->Type = mbi.Type;
    return TRUE;
}

static BOOL dump_query_memory(struct dump_context *dc, ULONG64 address, MINIDUMP_MEMORY_INFO *info)
{
    return dump_query_memory_status(dc, address, info, NULL);
}

static DWORD thread_write_flags(const struct dump_context *dc)
{
    DWORD flags = ThreadWriteThread | ThreadWriteStack | ThreadWriteContext | ThreadWriteInstructionWindow;
    if (dc->type & MiniDumpWithProcessThreadData) flags |= ThreadWriteThreadData;
    if (dc->type & MiniDumpWithThreadInfo) flags |= ThreadWriteThreadInfo;
    return flags;
}

static DWORD module_write_flags(const struct dump_context *dc)
{
    DWORD flags = ModuleWriteModule | ModuleWriteMiscRecord | ModuleWriteCvRecord;
    if (dc->type & MiniDumpWithDataSegs) flags |= ModuleWriteDataSeg;
    if (dc->type & MiniDumpWithProcessThreadData) flags |= ModuleWriteTlsData;
    if (dc->type & MiniDumpWithCodeSegs) flags |= ModuleWriteCodeSegs;
    return flags;
}

static BOOL cancel_requested(struct dump_context *dc)
{
    MINIDUMP_CALLBACK_INPUT input = {0};
    MINIDUMP_CALLBACK_OUTPUT output = {0};

    if (!dc->cb || !dc->check_cancel) return FAILED(dc->status);
    input.ProcessId = dc->pid;
    input.ProcessHandle = dc->callback_handle;
    input.CallbackType = CancelCallback;
    if (dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output))
    {
        dc->check_cancel = output.CheckCancel;
        if (output.Cancel) dc->status = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }
    return FAILED(dc->status);
}

/******************************************************************
 *		fetch_process_info
 *
 * reads system wide process information, and gather from it the threads information
 * for process of id 'pid'
 */
static BOOL fetch_process_info(struct dump_context* dc)
{
    ULONG       buf_size = 0x1000;
    NTSTATUS    nts;
    void*       pcs_buffer = NULL;

    if (!(pcs_buffer = HeapAlloc(GetProcessHeap(), 0, buf_size))) return FALSE;
    for (;;)
    {
        nts = NtQuerySystemInformation(SystemProcessInformation,
                                       pcs_buffer, buf_size, NULL);
        if (nts != STATUS_INFO_LENGTH_MISMATCH) break;
        pcs_buffer = HeapReAlloc(GetProcessHeap(), 0, pcs_buffer, buf_size *= 2);
        if (!pcs_buffer) return FALSE;
    }

    if (nts == STATUS_SUCCESS)
    {
        SYSTEM_PROCESS_INFORMATION*     spi = pcs_buffer;
        unsigned                        i;

        for (;;)
        {
            if (HandleToUlong(spi->UniqueProcessId) == dc->pid)
            {
                dc->num_threads = 0;
                dc->threads = HeapAlloc(GetProcessHeap(), 0,
                                        spi->dwThreadCount * sizeof(dc->threads[0]));
                if (!dc->threads) break;
                dc->thread_info = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                            spi->dwThreadCount * sizeof(*dc->thread_info));
                if (!dc->thread_info) break;
                for (i = 0; i < spi->dwThreadCount; i++)
                {
                    DWORD flags = thread_write_flags(dc);

                    /* don't include current thread */
                    if (HandleToULong(spi->ti[i].ClientId.UniqueThread) == GetCurrentThreadId())
                        continue;
                    if (dc->cb)
                    {
                        MINIDUMP_CALLBACK_INPUT input = {0};
                        MINIDUMP_CALLBACK_OUTPUT output = {0};

                        input.ProcessId = dc->pid;
                        input.ProcessHandle = dc->callback_handle;
                        input.CallbackType = IncludeThreadCallback;
                        input.IncludeThread.ThreadId = HandleToULong(spi->ti[i].ClientId.UniqueThread);
                        output.ThreadWriteFlags = flags;
                        if (!dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output)) continue;
                        flags &= output.ThreadWriteFlags;
                    }
                    dc->threads[dc->num_threads].tid        = HandleToULong(spi->ti[i].ClientId.UniqueThread);
                    dc->threads[dc->num_threads].prio_class = spi->ti[i].dwBasePriority; /* FIXME */
                    dc->threads[dc->num_threads].curr_prio  = spi->ti[i].dwCurrentPriority;
                    dc->threads[dc->num_threads].write_flags = flags;
                    dc->num_threads++;
                }
                HeapFree(GetProcessHeap(), 0, pcs_buffer);
                return TRUE;
            }
            if (!spi->NextEntryOffset) break;
            spi = (SYSTEM_PROCESS_INFORMATION*)((char*)spi + spi->NextEntryOffset);
        }
    }
    HeapFree(GetProcessHeap(), 0, pcs_buffer);
    return FALSE;
}

static void fetch_thread_stack(struct dump_context* dc, const void* teb_addr,
                               const CONTEXT* ctx, MINIDUMP_MEMORY_DESCRIPTOR* mmd)
{
    NT_TIB      tib;
    ADDRESS64   addr;

    if (read_process_memory(dc->process, (ULONG_PTR)teb_addr, &tib, sizeof(tib)) &&
        dbghelp_current_cpu &&
        dbghelp_current_cpu->get_addr(NULL /* FIXME */, ctx, cpu_addr_stack, &addr) && addr.Mode == AddrModeFlat)
    {
        if (addr.Offset)
        {
            addr.Offset -= dbghelp_current_cpu->word_size;
            /* make sure stack pointer is within the established range of the stack.  It could have
               been clobbered by whatever caused the original exception. */
            if (addr.Offset < (ULONG_PTR)tib.StackLimit || addr.Offset > (ULONG_PTR)tib.StackBase)
                mmd->StartOfMemoryRange = (ULONG_PTR)tib.StackLimit;

            else
                mmd->StartOfMemoryRange = addr.Offset;
        }
        else
            mmd->StartOfMemoryRange = (ULONG_PTR)tib.StackLimit;
        mmd->Memory.DataSize = (ULONG_PTR)tib.StackBase - mmd->StartOfMemoryRange;
    }
}

/******************************************************************
 *		fetch_thread_info
 *
 * fetches some information about thread of id 'tid'
 */
static BOOL fetch_thread_info(struct dump_context* dc, int thd_idx,
                              MINIDUMP_THREAD* mdThd, CONTEXT* ctx)
{
    DWORD                       tid = dc->threads[thd_idx].tid;
    HANDLE                      hThread;
    THREAD_BASIC_INFORMATION    tbi;
    MINIDUMP_THREAD_INFO        *info = &dc->thread_info[thd_idx];
    DWORD                       flags = dc->threads[thd_idx].write_flags;
    DWORD                       access = THREAD_QUERY_INFORMATION;
    NTSTATUS                    status;
    FILETIME                    create, exit, kernel, user;
    void                       *start;

    memset(ctx, 0, sizeof(*ctx));
    info->ThreadId = tid;
    if (tid == dc->writer_tid) info->DumpFlags |= MINIDUMP_THREAD_INFO_WRITING_THREAD;

    mdThd->ThreadId = tid;
    mdThd->SuspendCount = 0;
    mdThd->Teb = 0;
    mdThd->Stack.StartOfMemoryRange = 0;
    mdThd->Stack.Memory.DataSize = 0;
    mdThd->Stack.Memory.Rva = 0;
    mdThd->ThreadContext.DataSize = 0;
    mdThd->ThreadContext.Rva = 0;
    mdThd->PriorityClass = dc->threads[thd_idx].prio_class;
    mdThd->Priority = dc->threads[thd_idx].curr_prio;

    if (flags & (ThreadWriteStack | ThreadWriteContext | ThreadWriteInstructionWindow))
        access |= THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT;
    if ((hThread = OpenThread(access, FALSE, tid)) == NULL)
    {
        info->DumpFlags = MINIDUMP_THREAD_INFO_ERROR_THREAD;
        info->DumpError = HRESULT_FROM_WIN32(GetLastError());
        FIXME("Couldn't open thread %lu (%lu)\n", tid, GetLastError());
        return FALSE;
    }

    status = NtQueryInformationThread(hThread, ThreadBasicInformation, &tbi, sizeof(tbi), NULL);
    if (status == STATUS_SUCCESS)
    {
        info->ExitStatus = tbi.ExitStatus;
        info->Affinity = tbi.AffinityMask;
        mdThd->Teb = (ULONG_PTR)tbi.TebBaseAddress;
        if (tbi.ExitStatus == STILL_ACTIVE)
        {
            if (flags & (ThreadWriteStack | ThreadWriteContext | ThreadWriteInstructionWindow))
            {
                mdThd->SuspendCount = SuspendThread(hThread);
                if (mdThd->SuspendCount != (DWORD)-1)
                {
                    ctx->ContextFlags = CONTEXT_ALL;
                    if (!GetThreadContext(hThread, ctx))
                    {
                        info->DumpFlags |= MINIDUMP_THREAD_INFO_INVALID_CONTEXT;
                        info->DumpError = HRESULT_FROM_WIN32(GetLastError());
                        memset(ctx, 0, sizeof(*ctx));
                    }
                    if (flags & ThreadWriteStack)
                        fetch_thread_stack(dc, tbi.TebBaseAddress, ctx, &mdThd->Stack);
                    ResumeThread(hThread);
                }
                else
                {
                    info->DumpFlags |= MINIDUMP_THREAD_INFO_INVALID_CONTEXT;
                    info->DumpError = HRESULT_FROM_WIN32(GetLastError());
                }
            }
        }
        else
        {
            info->DumpFlags |= MINIDUMP_THREAD_INFO_EXITED_THREAD;
            mdThd->SuspendCount = (DWORD)-1;
        }
    }
    else
    {
        info->DumpFlags |= MINIDUMP_THREAD_INFO_INVALID_INFO;
        info->DumpError = HRESULT_FROM_NT(status);
    }
    if (flags & ThreadWriteThreadInfo)
    {
        if (GetThreadTimes(hThread, &create, &exit, &kernel, &user))
        {
            info->CreateTime = ((ULONG64)create.dwHighDateTime << 32) | create.dwLowDateTime;
            info->ExitTime = ((ULONG64)exit.dwHighDateTime << 32) | exit.dwLowDateTime;
            info->KernelTime = ((ULONG64)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
            info->UserTime = ((ULONG64)user.dwHighDateTime << 32) | user.dwLowDateTime;
        }
        else
        {
            info->DumpFlags |= MINIDUMP_THREAD_INFO_INVALID_INFO;
            info->DumpError = HRESULT_FROM_WIN32(GetLastError());
        }
        status = NtQueryInformationThread(hThread, ThreadQuerySetWin32StartAddress, &start, sizeof(start), NULL);
        if (!status) info->StartAddress = (ULONG_PTR)start;
        else
        {
            info->DumpFlags |= MINIDUMP_THREAD_INFO_INVALID_INFO;
            info->DumpError = HRESULT_FROM_NT(status);
        }
    }
    CloseHandle(hThread);
    return TRUE;
}

/******************************************************************
 *		add_module
 *
 * Add a module to a dump context
 */
static BOOL add_module(struct dump_context* dc, const WCHAR* name,
                       DWORD64 base, DWORD size, DWORD timestamp, DWORD checksum,
                       BOOL is_elf, DWORD flags)
{
    if (!dc->modules)
    {
        dc->alloc_modules = 32;
        dc->modules = HeapAlloc(GetProcessHeap(), 0,
                                dc->alloc_modules * sizeof(*dc->modules));
    }
    else if(dc->num_modules >= dc->alloc_modules)
    {
        dc->alloc_modules *= 2;
        dc->modules = HeapReAlloc(GetProcessHeap(), 0, dc->modules,
                                  dc->alloc_modules * sizeof(*dc->modules));
    }
    if (!dc->modules)
    {
        dc->alloc_modules = dc->num_modules = 0;
        return FALSE;
    }
    lstrcpynW(dc->modules[dc->num_modules].name, name,
              ARRAY_SIZE(dc->modules[dc->num_modules].name));
    dc->modules[dc->num_modules].base = base;
    dc->modules[dc->num_modules].size = size;
    dc->modules[dc->num_modules].timestamp = timestamp;
    dc->modules[dc->num_modules].checksum = checksum;
    dc->modules[dc->num_modules].is_elf = is_elf;
    dc->modules[dc->num_modules].write_flags = flags;
    dc->num_modules++;

    return TRUE;
}

/******************************************************************
 *		fetch_pe_module_info_cb
 *
 * Callback for accumulating in dump_context a PE modules set
 */
static BOOL WINAPI fetch_pe_module_info_cb(PCWSTR name, DWORD64 base, ULONG size,
                                           PVOID user)
{
    struct dump_context*        dc = user;
    IMAGE_NT_HEADERS            nth;
    DWORD                       flags = module_write_flags(dc);

    if (!validate_addr64(base)) return FALSE;
    if (dc->cb)
    {
        MINIDUMP_CALLBACK_INPUT input = {0};
        MINIDUMP_CALLBACK_OUTPUT output = {0};

        input.ProcessId = dc->pid;
        input.ProcessHandle = dc->callback_handle;
        input.CallbackType = IncludeModuleCallback;
        input.IncludeModule.BaseOfImage = base;
        output.ModuleWriteFlags = flags;
        if (!dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output)) return TRUE;
        flags &= output.ModuleWriteFlags;
    }

    if (pe_load_nt_header(dc->process, base, &nth, NULL))
        add_module(user, name, base, size,
                   nth.FileHeader.TimeDateStamp, nth.OptionalHeader.CheckSum,
                   FALSE, flags);
    return TRUE;
}

/******************************************************************
 *		fetch_elf_module_info_cb
 *
 * Callback for accumulating in dump_context an host modules set
 */
static BOOL fetch_host_module_info_cb(const WCHAR* name, ULONG_PTR base,
                                     void* user)
{
    struct dump_context*        dc = user;
    DWORD_PTR                   rbase;
    DWORD                       size, checksum;

    /* FIXME: there's no relevant timestamp on ELF modules */
    if (!dc->process->loader->fetch_file_info(dc->process, name, base, &rbase, &size, &checksum))
        size = checksum = 0;
    add_module(dc, name, base ? base : rbase, size, 0 /* FIXME */, checksum, TRUE, module_write_flags(dc));
    return TRUE;
}

struct memory64_plan
{
    struct dump_memory64 *blocks;
    unsigned count;
    unsigned capacity;
};

static BOOL minidump_add_memory64_block(struct dump_context *dc, struct memory64_plan *plan,
                                       ULONG64 base, ULONG64 size)
{
    if (!size || dc->status != S_OK) return FALSE;
    if (base > ~(ULONG64)0 - size)
    {
        dc->status = E_INVALIDARG;
        return FALSE;
    }
    if (plan->count == plan->capacity)
    {
        unsigned int count = plan->capacity ? plan->capacity * 2 : 32;
        struct dump_memory64 *mem;
        SIZE_T bytes = (SIZE_T)count * sizeof(*mem);

        if (count <= plan->capacity || bytes / sizeof(*mem) != count)
        {
            dc->status = E_OUTOFMEMORY;
            return FALSE;
        }
        mem = plan->blocks ? HeapReAlloc(GetProcessHeap(), 0, plan->blocks, bytes) :
                             HeapAlloc(GetProcessHeap(), 0, bytes);
        if (!mem)
        {
            dc->status = E_OUTOFMEMORY;
            return FALSE;
        }
        plan->blocks = mem;
        plan->capacity = count;
    }
    plan->blocks[plan->count].base = base;
    plan->blocks[plan->count].size = size;
    ++plan->count;
    return TRUE;
}

static BOOL fetch_memory64_info(struct dump_context *dc, struct memory64_plan *plan)
{
    ULONG64 addr = 0, end;
    MINIDUMP_MEMORY_INFO info;
    HRESULT status;

    plan->count = 0;
    while (!cancel_requested(dc))
    {
        if (!dump_query_memory_status(dc, addr, &info, &status))
        {
            if (status != S_OK) dc->status = status;
            break;
        }
        if (!info.RegionSize || info.BaseAddress > addr ||
            info.RegionSize > ~(ULONG64)0 - info.BaseAddress) break;
        end = info.BaseAddress + info.RegionSize;
        if (end <= addr) break;
        if (info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
            !minidump_add_memory64_block(dc, plan, info.BaseAddress, info.RegionSize)) return FALSE;
        addr = end;
    }
    return dc->status == S_OK;
}

static void fetch_modules_info(struct dump_context* dc)
{
    EnumerateLoadedModulesW64(dc->process->handle, fetch_pe_module_info_cb, dc);
    /* Since we include ELF modules in a separate stream from the regular PE ones,
     * we can always include those ELF modules (they don't eat lots of space)
     * And it's always a good idea to have a trace of the loaded ELF modules for
     * a given application in a post mortem debugging condition.
     */
    dc->process->loader->enum_modules(dc->process, fetch_host_module_info_cb, dc);
}

static void fetch_module_versioninfo(LPCWSTR filename, VS_FIXEDFILEINFO* ffi)
{
    DWORD       handle;
    DWORD       sz;

    memset(ffi, 0, sizeof(*ffi));
    if ((sz = GetFileVersionInfoSizeW(filename, &handle)))
    {
        void*   info = HeapAlloc(GetProcessHeap(), 0, sz);
        if (info && GetFileVersionInfoW(filename, handle, sz, info))
        {
            VS_FIXEDFILEINFO*   ptr;
            UINT    len;

            if (VerQueryValueW(info, L"\\", (void*)&ptr, &len))
                memcpy(ffi, ptr, min(len, sizeof(*ffi)));
        }
        HeapFree(GetProcessHeap(), 0, info);
    }
}

/******************************************************************
 *		minidump_add_memory_block
 *
 * Add a memory block to be dumped in a minidump
 * If rva is non 0, it's the rva in the minidump where has to be stored
 * also the rva of the memory block when written (this allows us to reference
 * a memory block from outside the list of memory blocks).
 */
void minidump_add_memory_block(struct dump_context* dc, ULONG64 base, ULONG size, ULONG rva)
{
    if (!size || FAILED(dc->status)) return;
    if (base > ~(ULONG64)0 - size)
    {
        dc->status = E_INVALIDARG;
        return;
    }
    if (!rva)
    {
        BYTE byte;
        ULONG offset = 0, start = 0, length = 0, step;

        while (offset < size)
        {
            step = min(size - offset, sysinfo.dwPageSize - (base + offset) % sysinfo.dwPageSize);
            if (read_process_memory(dc->process, base + offset, &byte, sizeof(byte)))
                length += step;
            else if (length)
                break;
            else
                start = offset + step;
            offset += step;
        }
        if (!length) return;
        base += start;
        size = length;
    }
    if (dc->num_mem == dc->alloc_mem)
    {
        unsigned int count = dc->alloc_mem ? dc->alloc_mem * 2 : 32;
        struct dump_memory *mem;
        SIZE_T bytes = (SIZE_T)count * sizeof(*mem);

        if (count <= dc->alloc_mem || bytes / sizeof(*mem) != count)
        {
            dc->status = E_OUTOFMEMORY;
            return;
        }
        mem = dc->mem ? HeapReAlloc(GetProcessHeap(), 0, dc->mem, bytes) :
                       HeapAlloc(GetProcessHeap(), 0, bytes);
        if (!mem)
        {
            dc->status = E_OUTOFMEMORY;
            return;
        }
        dc->mem = mem;
        dc->alloc_mem = count;
    }
    dc->mem[dc->num_mem].base = base;
    dc->mem[dc->num_mem].size = size;
    dc->mem[dc->num_mem].rva = rva;
    ++dc->num_mem;
}

/******************************************************************
 *		writeat
 *
 * Writes a chunk of data at a given position in the minidump
 */
static void writeat(struct dump_context* dc, ULONG64 rva, const void* data, unsigned size)
{
    DWORD       written;
    LARGE_INTEGER offset;

    if (FAILED(dc->status)) return;
    if (dc->callback_io)
    {
        MINIDUMP_CALLBACK_INPUT input = {0};
        MINIDUMP_CALLBACK_OUTPUT output = {0};

        input.ProcessId = dc->pid;
        input.ProcessHandle = dc->callback_handle;
        input.CallbackType = IoWriteAllCallback;
        input.Io.Handle = dc->hFile;
        input.Io.Offset = rva;
        input.Io.Buffer = (void *)data;
        input.Io.BufferBytes = size;
        output.Status = E_FAIL;
        if (!dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output))
            dc->status = E_FAIL;
        else if (output.Status != S_OK)
            dc->status = FAILED(output.Status) ? output.Status : E_FAIL;
    }
    else
    {
        offset.QuadPart = rva;
        if (!SetFilePointerEx(dc->hFile, offset, NULL, FILE_BEGIN) ||
            !WriteFile(dc->hFile, data, size, &written, NULL))
            dc->status = HRESULT_FROM_WIN32(GetLastError());
        else if (written != size)
            dc->status = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    }
}

/******************************************************************
 *		append
 *
 * writes a new chunk of data to the minidump, increasing the current
 * rva in dc
 */
static void append(struct dump_context* dc, const void* data, unsigned size)
{
    writeat(dc, dc->rva, data, size);
    dc->rva += size;
}

static void fetch_shared_user_data(struct dump_context *dc)
{
    MINIDUMP_MEMORY_INFO info;
    const ULONG64 address = 0x7ffe0000;

    if (dump_query_memory(dc, address, &info) && info.State == MEM_COMMIT &&
        !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && info.BaseAddress <= address &&
        address - info.BaseAddress <= info.RegionSize &&
        sizeof(KUSER_SHARED_DATA) <= info.RegionSize - (address - info.BaseAddress))
        minidump_add_memory_block(dc, address, sizeof(KUSER_SHARED_DATA), 0);
}

static void filter_memory(struct dump_context *dc)
{
    MINIDUMP_CALLBACK_INPUT input = {0};
    MINIDUMP_CALLBACK_OUTPUT output;
    unsigned int i;

    if (!dc->cb) return;
    input.ProcessId = dc->pid;
    input.ProcessHandle = dc->callback_handle;
    input.CallbackType = MemoryCallback;
    for (;;)
    {
        memset(&output, 0, sizeof(output));
        if (cancel_requested(dc) || !dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output) ||
            !output.MemorySize) break;
        if (output.MemoryBase > ~(ULONG64)0 - output.MemorySize)
        {
            dc->status = E_INVALIDARG;
            return;
        }
        minidump_add_memory_block(dc, output.MemoryBase, output.MemorySize, 0);
    }
    input.CallbackType = RemoveMemoryCallback;
    for (;;)
    {
        ULONG64 end;

        memset(&output, 0, sizeof(output));
        if (cancel_requested(dc) || !dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output) ||
            !output.MemorySize) break;
        if (output.MemoryBase > ~(ULONG64)0 - output.MemorySize)
        {
            dc->status = E_INVALIDARG;
            return;
        }
        end = output.MemoryBase + output.MemorySize;
        for (i = 0; i < dc->num_mem; ++i)
        {
            struct dump_memory block = dc->mem[i];
            ULONG64 block_end = block.base + block.size;

            if (end <= block.base || output.MemoryBase >= block_end) continue;
            if (output.MemoryBase > block.base)
            {
                dc->mem[i].size = output.MemoryBase - block.base;
                if (end < block_end) minidump_add_memory_block(dc, end, block_end - end, 0);
            }
            else if (end < block_end)
            {
                dc->mem[i].base = end;
                dc->mem[i].size = block_end - end;
            }
            else dc->mem[i].size = 0;
            if (block.rva)
            {
                MINIDUMP_MEMORY_DESCRIPTOR descriptor;

                descriptor.StartOfMemoryRange = dc->mem[i].size ? dc->mem[i].base : 0;
                descriptor.Memory.DataSize = dc->mem[i].size;
                descriptor.Memory.Rva = 0;
                writeat(dc, block.rva - FIELD_OFFSET(MINIDUMP_MEMORY_DESCRIPTOR, Memory.Rva),
                        &descriptor, sizeof(descriptor));
            }
            if (!dc->mem[i].size)
            {
                memmove(&dc->mem[i], &dc->mem[i + 1], (--dc->num_mem - i) * sizeof(*dc->mem));
                --i;
            }
        }
    }
}

/******************************************************************
 *		dump_exception_info
 *
 * Write in File the exception information from pcs
 */
static  unsigned        dump_exception_info(struct dump_context* dc)
{
    MINIDUMP_EXCEPTION_STREAM   mdExcpt;
    EXCEPTION_RECORD            rec, *prec;
    CONTEXT                     ctx, *pctx;
    DWORD                       i;

    mdExcpt.ThreadId = dc->except_param->ThreadId;
    mdExcpt.__alignment = 0;
    if (dc->except_param->ClientPointers)
    {
        EXCEPTION_POINTERS      ep;

        if (!read_process_memory(dc->process, (ULONG_PTR)dc->except_param->ExceptionPointers, &ep, sizeof(ep)) ||
            !read_process_memory(dc->process, (ULONG_PTR)ep.ExceptionRecord, &rec, sizeof(rec)) ||
            !read_process_memory(dc->process, (ULONG_PTR)ep.ContextRecord, &ctx, sizeof(ctx)))
        {
            dc->status = HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY);
            return 0;
        }
        prec = &rec;
        pctx = &ctx;
    }
    else
    {
        prec = dc->except_param->ExceptionPointers->ExceptionRecord;
        pctx = dc->except_param->ExceptionPointers->ContextRecord;
    }
    mdExcpt.ExceptionRecord.ExceptionCode = prec->ExceptionCode;
    mdExcpt.ExceptionRecord.ExceptionFlags = prec->ExceptionFlags;
    mdExcpt.ExceptionRecord.ExceptionRecord = (DWORD_PTR)prec->ExceptionRecord;
    mdExcpt.ExceptionRecord.ExceptionAddress = (DWORD_PTR)prec->ExceptionAddress;
    mdExcpt.ExceptionRecord.NumberParameters = prec->NumberParameters;
    mdExcpt.ExceptionRecord.__unusedAlignment = 0;
    for (i = 0; i < mdExcpt.ExceptionRecord.NumberParameters; i++)
        mdExcpt.ExceptionRecord.ExceptionInformation[i] = prec->ExceptionInformation[i];
    mdExcpt.ThreadContext.DataSize = sizeof(*pctx);
    mdExcpt.ThreadContext.Rva = dc->rva + sizeof(mdExcpt);

    append(dc, &mdExcpt, sizeof(mdExcpt));
    append(dc, pctx, sizeof(*pctx));
    return sizeof(mdExcpt);
}

/******************************************************************
 *		dump_modules
 *
 * Write in File the modules from pcs
 */
static void fetch_module_sections(struct dump_context *dc, const struct dump_module *module, DWORD flags)
{
    IMAGE_DOS_HEADER dos;
    IMAGE_FILE_HEADER file;
    IMAGE_SECTION_HEADER section;
    ULONG64 offset;
    DWORD signature, size;
    unsigned int i;

    if (module->is_elf || !(flags & (ModuleWriteCodeSegs | ModuleWriteDataSeg))) return;
    if (!read_process_memory(dc->process, module->base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 0 || (ULONG)dos.e_lfanew > module->size ||
        module->size - dos.e_lfanew < sizeof(signature) + sizeof(file)) return;
    offset = dos.e_lfanew;
    if (!read_process_memory(dc->process, module->base + offset, &signature, sizeof(signature)) ||
        signature != IMAGE_NT_SIGNATURE ||
        !read_process_memory(dc->process, module->base + offset + sizeof(signature), &file, sizeof(file))) return;
    offset += sizeof(signature) + sizeof(file) + file.SizeOfOptionalHeader;
    if (offset > module->size || file.NumberOfSections > (module->size - offset) / sizeof(section)) return;
    for (i = 0; i < file.NumberOfSections; ++i, offset += sizeof(section))
    {
        if (!read_process_memory(dc->process, module->base + offset, &section, sizeof(section))) break;
        if (!((flags & ModuleWriteCodeSegs) && (section.Characteristics & IMAGE_SCN_CNT_CODE)) &&
            !((flags & ModuleWriteDataSeg) && (section.Characteristics & IMAGE_SCN_MEM_WRITE))) continue;
        size = section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData;
        if (section.VirtualAddress >= module->size) continue;
        size = min(size, module->size - section.VirtualAddress);
        if (size) minidump_add_memory_block(dc, module->base + section.VirtualAddress, size, 0);
    }
}

static  unsigned        dump_modules(struct dump_context* dc, BOOL dump_elf)
{
    MINIDUMP_MODULE             mdModule;
    MINIDUMP_MODULE_LIST        mdModuleList;
    char                        tmp[1024];
    MINIDUMP_STRING*            ms = (MINIDUMP_STRING*)tmp;
    ULONG                       i, nmod;
    RVA                         rva_base;
    DWORD                       flags_out;
    unsigned                    sz;

    for (i = nmod = 0; i < dc->num_modules; i++)
    {
        if ((dc->modules[i].is_elf && dump_elf) ||
            (!dc->modules[i].is_elf && !dump_elf))
            nmod++;
    }

    mdModuleList.NumberOfModules = 0;
    /* reserve space for mdModuleList
     * FIXME: since we don't support 0 length arrays, we cannot use the
     * size of mdModuleList
     * FIXME: if we don't ask for all modules in cb, we'll get a hole in the file
     */

    /* the stream size is just the size of the module index.  It does not include the data for the
       names of each module.  *Technically* the names are supposed to go into the common string table
       in the minidump file.  Since each string is referenced by RVA they can all safely be located
       anywhere between streams in the file, so the end of this stream is sufficient. */
    rva_base = dc->rva;
    dc->rva += sz = sizeof(mdModuleList.NumberOfModules) + sizeof(mdModule) * nmod;
    for (i = 0; i < dc->num_modules; i++)
    {
        if ((dc->modules[i].is_elf && !dump_elf) ||
            (!dc->modules[i].is_elf && dump_elf))
            continue;

        if (cancel_requested(dc)) break;
        flags_out = dc->modules[i].write_flags;
        ms->Length = (lstrlenW(dc->modules[i].name) + 1) * sizeof(WCHAR);
        if (sizeof(ULONG) + ms->Length > sizeof(tmp))
            FIXME("Buffer overflow!!!\n");
        lstrcpyW(ms->Buffer, dc->modules[i].name);

        if (dc->cb)
        {
            MINIDUMP_CALLBACK_INPUT     cbin;
            MINIDUMP_CALLBACK_OUTPUT    cbout;

            cbin.ProcessId = dc->pid;
            cbin.ProcessHandle = dc->callback_handle;
            cbin.CallbackType = ModuleCallback;

            cbin.Module.FullPath = ms->Buffer;
            cbin.Module.BaseOfImage = dc->modules[i].base;
            cbin.Module.SizeOfImage = dc->modules[i].size;
            cbin.Module.CheckSum = dc->modules[i].checksum;
            cbin.Module.TimeDateStamp = dc->modules[i].timestamp;
            memset(&cbin.Module.VersionInfo, 0, sizeof(cbin.Module.VersionInfo));
            cbin.Module.CvRecord = NULL;
            cbin.Module.SizeOfCvRecord = 0;
            cbin.Module.MiscRecord = NULL;
            cbin.Module.SizeOfMiscRecord = 0;

            cbout.ModuleWriteFlags = flags_out;
            if (!dc->cb->CallbackRoutine(dc->cb->CallbackParam, &cbin, &cbout))
                continue;
            flags_out &= cbout.ModuleWriteFlags;
        }
        if (flags_out & ModuleWriteModule)
        {
            fetch_module_sections(dc, &dc->modules[i], flags_out);
            /* fetch CPU dependent module info (like UNWIND_INFO) */
            dbghelp_current_cpu->fetch_minidump_module(dc, i, flags_out);

            mdModule.BaseOfImage = dc->modules[i].base;
            mdModule.SizeOfImage = dc->modules[i].size;
            mdModule.CheckSum = dc->modules[i].checksum;
            mdModule.TimeDateStamp = dc->modules[i].timestamp;
            mdModule.ModuleNameRva = dc->rva;
            ms->Length -= sizeof(WCHAR);
            append(dc, ms, sizeof(ULONG) + ms->Length + sizeof(WCHAR));
            if (!dump_elf) fetch_module_versioninfo(ms->Buffer, &mdModule.VersionInfo);
            else memset(&mdModule.VersionInfo, 0, sizeof(mdModule.VersionInfo));
            mdModule.CvRecord.DataSize = 0; /* FIXME */
            mdModule.CvRecord.Rva = 0; /* FIXME */
            mdModule.MiscRecord.DataSize = 0; /* FIXME */
            mdModule.MiscRecord.Rva = 0; /* FIXME */
            mdModule.Reserved0 = 0; /* FIXME */
            mdModule.Reserved1 = 0; /* FIXME */
            writeat(dc,
                    rva_base + sizeof(mdModuleList.NumberOfModules) + 
                        mdModuleList.NumberOfModules++ * sizeof(mdModule), 
                    &mdModule, sizeof(mdModule));
        }
    }
    writeat(dc, rva_base, &mdModuleList.NumberOfModules, 
            sizeof(mdModuleList.NumberOfModules));

    return sz;
}


/******************************************************************
 *		dump_system_info
 *
 * Dumps into File the information about the system
 */
static  unsigned        dump_system_info(struct dump_context* dc)
{
    MINIDUMP_SYSTEM_INFO        mdSysInfo;
    SYSTEM_INFO                 sysInfo;
    RTL_OSVERSIONINFOEXW        osInfo;
    ULONG                       slen;
    DWORD                       wine_extra = 0;

    const char *(CDECL *wine_get_build_id)(void);
    void (CDECL *wine_get_host_version)(const char **sysname, const char **release);
    const char* build_id = NULL;
    const char* sys_name = NULL;
    const char* release_name = NULL;

    GetSystemInfo(&sysInfo);
    osInfo.dwOSVersionInfoSize = sizeof(osInfo);
    RtlGetVersion(&osInfo);

    wine_get_build_id = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_build_id");
    wine_get_host_version = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_host_version");
    if (wine_get_build_id && wine_get_host_version)
    {
        /* cheat minidump system information by adding specific wine information */
        wine_extra = 4 + 4 * sizeof(slen);
        build_id = wine_get_build_id();
        wine_get_host_version(&sys_name, &release_name);
        wine_extra += strlen(build_id) + 1 + strlen(sys_name) + 1 + strlen(release_name) + 1;
    }

    mdSysInfo.ProcessorArchitecture = sysInfo.wProcessorArchitecture;
    mdSysInfo.ProcessorLevel = sysInfo.wProcessorLevel;
    mdSysInfo.ProcessorRevision = sysInfo.wProcessorRevision;
    mdSysInfo.NumberOfProcessors = sysInfo.dwNumberOfProcessors;
    mdSysInfo.ProductType = VER_NT_WORKSTATION; /* FIXME */
    mdSysInfo.MajorVersion = osInfo.dwMajorVersion;
    mdSysInfo.MinorVersion = osInfo.dwMinorVersion;
    mdSysInfo.BuildNumber = osInfo.dwBuildNumber;
    mdSysInfo.PlatformId = osInfo.dwPlatformId;

    mdSysInfo.CSDVersionRva = dc->rva + sizeof(mdSysInfo) + wine_extra;
    mdSysInfo.Reserved1 = 0;
    mdSysInfo.SuiteMask = VER_SUITE_TERMINAL;

#if defined(__i386__) || (defined(__x86_64__) && !defined(__arm64ec__))
    {
        int regs[4];

        __cpuid(regs, 0);
        mdSysInfo.Cpu.X86CpuInfo.VendorId[0] = regs[1];
        mdSysInfo.Cpu.X86CpuInfo.VendorId[1] = regs[3];
        mdSysInfo.Cpu.X86CpuInfo.VendorId[2] = regs[2];
        __cpuid(regs, 1);
        mdSysInfo.Cpu.X86CpuInfo.VersionInformation = regs[0];
        mdSysInfo.Cpu.X86CpuInfo.FeatureInformation = regs[3];
        mdSysInfo.Cpu.X86CpuInfo.AMDExtendedCpuFeatures = 0;
        if (!memcmp( mdSysInfo.Cpu.X86CpuInfo.VendorId, "AuthenticAMD", 12 ))
        {
            __cpuid(regs, 0x80000001);  /* get vendor features */
            mdSysInfo.Cpu.X86CpuInfo.AMDExtendedCpuFeatures = regs[3];
        }
    }
#else
    mdSysInfo.Cpu.OtherCpuInfo.ProcessorFeatures[0] = 0;
    mdSysInfo.Cpu.OtherCpuInfo.ProcessorFeatures[1] = 0;
    for (unsigned int i = 0; i < sizeof(mdSysInfo.Cpu.OtherCpuInfo.ProcessorFeatures[0]) * 8; i++)
        if (IsProcessorFeaturePresent(i))
            mdSysInfo.Cpu.OtherCpuInfo.ProcessorFeatures[0] |= (ULONG64)1 << i;
#endif
    append(dc, &mdSysInfo, sizeof(mdSysInfo));

    /* write Wine specific system information just behind the structure, and before any string */
    if (wine_extra)
    {
        static const char code[] = {'W','I','N','E'};

        append(dc, code, 4);
        /* number of sub-info, so that we can extend structure if needed */
        slen = 3;
        append(dc, &slen, sizeof(slen));
        /* we store offsets from just after the WINE marker */
        slen = 4 * sizeof(DWORD);
        append(dc, &slen, sizeof(slen));
        slen += strlen(build_id) + 1;
        append(dc, &slen, sizeof(slen));
        slen += strlen(sys_name) + 1;
        append(dc, &slen, sizeof(slen));
        append(dc, build_id, strlen(build_id) + 1);
        append(dc, sys_name, strlen(sys_name) + 1);
        append(dc, release_name, strlen(release_name) + 1);
    }

    /* write the service pack version string after this stream.  It is referenced within the
       stream by its RVA in the file. */
    slen = lstrlenW(osInfo.szCSDVersion) * sizeof(WCHAR);
    append(dc, &slen, sizeof(slen));
    append(dc, osInfo.szCSDVersion, slen);

    return sizeof(mdSysInfo);
}

/******************************************************************
 *		dump_threads
 *
 * Dumps into File the information about running threads
 */
static  unsigned        dump_threads(struct dump_context* dc)
{
    MINIDUMP_THREAD             mdThd;
    MINIDUMP_THREAD_LIST        mdThdList;
    unsigned                    i, sz;
    RVA                         rva_base;
    DWORD                       flags_out;
    CONTEXT                     ctx;

    mdThdList.NumberOfThreads = 0;

    rva_base = dc->rva;
    dc->rva += sz = sizeof(mdThdList.NumberOfThreads) + dc->num_threads * sizeof(mdThd);

    for (i = 0; i < dc->num_threads; i++)
    {
        if (cancel_requested(dc)) break;
        fetch_thread_info(dc, i, &mdThd, &ctx);

        flags_out = dc->threads[i].write_flags;

        if (dc->cb)
        {
            MINIDUMP_CALLBACK_INPUT     cbin;
            MINIDUMP_CALLBACK_OUTPUT    cbout;

            cbin.ProcessId = dc->pid;
            cbin.ProcessHandle = dc->callback_handle;
            cbin.CallbackType = ThreadCallback;
            cbin.Thread.ThreadId = dc->threads[i].tid;
            cbin.Thread.ThreadHandle = 0; /* FIXME */
            cbin.Thread.Context = ctx;
            cbin.Thread.SizeOfContext = sizeof(CONTEXT);
            cbin.Thread.StackBase = mdThd.Stack.StartOfMemoryRange;
            cbin.Thread.StackEnd = mdThd.Stack.StartOfMemoryRange +
                mdThd.Stack.Memory.DataSize;

            cbout.ThreadWriteFlags = flags_out;
            if (!dc->cb->CallbackRoutine(dc->cb->CallbackParam, &cbin, &cbout))
            {
                dc->threads[i].write_flags = 0;
                continue;
            }
            flags_out &= cbout.ThreadWriteFlags;
        }
        dc->threads[i].write_flags = flags_out;
        if (!(flags_out & ThreadWriteStack)) memset(&mdThd.Stack, 0, sizeof(mdThd.Stack));
        if (flags_out & ThreadWriteThread)
        {
            if (ctx.ContextFlags && (flags_out & ThreadWriteContext))
            {
                mdThd.ThreadContext.Rva = dc->rva;
                mdThd.ThreadContext.DataSize = sizeof(CONTEXT);
                append(dc, &ctx, sizeof(CONTEXT));
            }
            if (mdThd.Stack.Memory.DataSize && (flags_out & ThreadWriteStack))
            {
                minidump_add_memory_block(dc, mdThd.Stack.StartOfMemoryRange,
                                          mdThd.Stack.Memory.DataSize,
                                          rva_base + sizeof(mdThdList.NumberOfThreads) +
                                          mdThdList.NumberOfThreads * sizeof(mdThd) +
                                          FIELD_OFFSET(MINIDUMP_THREAD, Stack.Memory.Rva));
            }
            writeat(dc, 
                    rva_base + sizeof(mdThdList.NumberOfThreads) +
                        mdThdList.NumberOfThreads * sizeof(mdThd),
                    &mdThd, sizeof(mdThd));
            mdThdList.NumberOfThreads++;
        }
        /* fetch CPU dependent thread info (like 256 bytes around program counter */
        dbghelp_current_cpu->fetch_minidump_thread(dc, i, flags_out, &ctx);
    }
    writeat(dc, rva_base,
            &mdThdList.NumberOfThreads, sizeof(mdThdList.NumberOfThreads));

    return sz;
}

static unsigned dump_thread_info(struct dump_context *dc)
{
    MINIDUMP_THREAD_INFO_LIST list = {sizeof(list), sizeof(MINIDUMP_THREAD_INFO), 0};
    unsigned int i;

    for (i = 0; i < dc->num_threads; ++i)
        if (dc->threads[i].write_flags & ThreadWriteThreadInfo) ++list.NumberOfEntries;
    append(dc, &list, sizeof(list));
    for (i = 0; i < dc->num_threads; ++i)
        if (dc->threads[i].write_flags & ThreadWriteThreadInfo)
            append(dc, &dc->thread_info[i], sizeof(*dc->thread_info));
    return sizeof(list) + list.NumberOfEntries * sizeof(MINIDUMP_THREAD_INFO);
}

/******************************************************************
 *		dump_threads_names
 *
 * Dumps into File the information about threads's name
 */
static  unsigned        dump_threads_names(struct dump_context* dc)
{
    MINIDUMP_THREAD_NAME        md_thread_name;
    MINIDUMP_THREAD_NAME_LIST   md_thread_name_list;
    unsigned                    i, sz;
    RVA                         rva_base;

    /* FIXME this could be optimized
     * (we use dc->num_threads disk space, could be optimized to the number of threads with name)
     */

    rva_base = dc->rva;
    dc->rva += sz = offsetof(MINIDUMP_THREAD_NAME_LIST, ThreadNames[dc->num_threads]);

    md_thread_name_list.NumberOfThreadNames = 0;

    for (i = 0; i < dc->num_threads; i++)
    {
        HANDLE  thread;
        WCHAR  *thread_name;

        if (!(dc->threads[i].write_flags & ThreadWriteThread)) continue;
        if ((thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, dc->threads[i].tid)) != NULL)
        {
            if (SUCCEEDED(GetThreadDescription(thread, &thread_name)))
            {
                MINIDUMP_STRING md_string;

                if (!thread_name || !*thread_name)
                {
                    LocalFree(thread_name);
                    CloseHandle(thread);
                    continue;
                }
                md_thread_name.ThreadId = dc->threads[i].tid;
                md_thread_name.RvaOfThreadName = dc->rva;

                md_string.Length = wcslen(thread_name) * sizeof(WCHAR);
                append(dc, &md_string.Length, sizeof(md_string.Length));
                append(dc, thread_name, md_string.Length);

                writeat(dc,
                        rva_base + offsetof(MINIDUMP_THREAD_NAME_LIST, ThreadNames[md_thread_name_list.NumberOfThreadNames]),
                        &md_thread_name, sizeof(md_thread_name));
                md_thread_name_list.NumberOfThreadNames++;
                LocalFree(thread_name);
            }
            CloseHandle(thread);
        }
    }
    if (!md_thread_name_list.NumberOfThreadNames) return 0;

    writeat(dc, rva_base, &md_thread_name_list.NumberOfThreadNames, sizeof(md_thread_name_list.NumberOfThreadNames));
    return sz;
}

/******************************************************************
 *		dump_memory_info
 *
 * dumps information about the memory of the process (stack of the threads)
 */
static unsigned         dump_memory_info(struct dump_context* dc)
{
    MINIDUMP_MEMORY_DESCRIPTOR mdMem;
    ULONG i, written = 0;
    RVA list_rva = dc->rva, rva_base;
    BYTE *buffer;

    if (dc->rva > MAXDWORD - sizeof(written) ||
        dc->num_mem > (MAXDWORD - dc->rva - sizeof(written)) / sizeof(mdMem))
    {
        dc->status = E_OUTOFMEMORY;
        return 0;
    }
    append(dc, &written, sizeof(written));
    rva_base = dc->rva;
    dc->rva += dc->num_mem * sizeof(mdMem);
    for (i = 0; i < dc->num_mem && SUCCEEDED(dc->status); ++i)
    {
        mdMem.StartOfMemoryRange = dc->mem[i].base;
        mdMem.Memory.Rva = dc->rva;
        mdMem.Memory.DataSize = dc->mem[i].size;
        if (mdMem.Memory.DataSize > MAXDWORD - dc->rva ||
            !(buffer = HeapAlloc(GetProcessHeap(), 0, mdMem.Memory.DataSize)))
        {
            dc->status = E_OUTOFMEMORY;
            return 0;
        }
        if (!read_process_memory(dc->process, mdMem.StartOfMemoryRange, buffer, mdMem.Memory.DataSize))
        {
            DWORD error = GetLastError();

            HeapFree(GetProcessHeap(), 0, buffer);
            if (dc->mem[i].rva)
            {
                dc->status = HRESULT_FROM_WIN32(error);
                return 0;
            }
            SetLastError(error);
            if (!dump_ignore_memory_failure(dc, mdMem.StartOfMemoryRange, mdMem.Memory.DataSize)) return 0;
            continue;
        }
        append(dc, buffer, mdMem.Memory.DataSize);
        HeapFree(GetProcessHeap(), 0, buffer);
        writeat(dc, rva_base + written++ * sizeof(mdMem), &mdMem, sizeof(mdMem));
        if (dc->mem[i].rva)
            writeat(dc, dc->mem[i].rva, &mdMem.Memory.Rva, sizeof(mdMem.Memory.Rva));
    }
    writeat(dc, list_rva, &written, sizeof(written));
    return sizeof(written) + written * sizeof(mdMem);
}

static BOOL dump_read_memory64(struct dump_context *dc, ULONG64 address, void *buffer, ULONG size)
{
    DWORD error = ERROR_READ_FAULT;
    unsigned attempt;

    for (attempt = 0; attempt < 5; ++attempt)
    {
        if (cancel_requested(dc)) return FALSE;
        if (read_process_memory(dc->process, address, buffer, size)) return TRUE;
        error = GetLastError();
        Sleep(250);
    }
    if (!cancel_requested(dc)) dc->status = HRESULT_FROM_WIN32(error);
    return FALSE;
}

/******************************************************************
 *		dump_memory64_info
 *
 * dumps information about the memory of the process (virtual memory)
 */
static unsigned         dump_memory64_info(struct dump_context* dc)
{
    struct memory64_plan plan = {0}, actual = {0};
    MINIDUMP_MEMORY_DESCRIPTOR64 descriptor;
    MINIDUMP_MEMORY_INFO info;
    ULONG64 addr, end, pos, filepos, count, data_rva;
    RVA stream_rva = dc->rva, descriptors_rva;
    ULONG len, attempt, result = 0;
    HRESULT status;
    BYTE *buffer;

    if (dc->status != S_OK) return 0;
    if (!(buffer = HeapAlloc(GetProcessHeap(), 0, 65536)))
    {
        dc->status = E_OUTOFMEMORY;
        return 0;
    }
    for (attempt = 0; attempt < 5 && !cancel_requested(dc); ++attempt)
    {
        if (!fetch_memory64_info(dc, &plan)) break;
        if (stream_rva > MAXDWORD - 2 * sizeof(ULONG64) ||
            plan.count > (MAXDWORD - stream_rva - 2 * sizeof(ULONG64)) / sizeof(descriptor))
        {
            dc->status = E_OUTOFMEMORY;
            break;
        }
        count = plan.count;
        data_rva = stream_rva + 2 * sizeof(ULONG64) + plan.count * sizeof(descriptor);
        dc->rva = stream_rva;
        append(dc, &count, sizeof(count));
        append(dc, &data_rva, sizeof(data_rva));
        descriptors_rva = dc->rva;
        dc->rva = data_rva;
        filepos = data_rva;
        actual.count = 0;
        addr = 0;
        while (!cancel_requested(dc))
        {
            if (!dump_query_memory_status(dc, addr, &info, &status))
            {
                if (status != S_OK) dc->status = status;
                break;
            }
            if (!info.RegionSize || info.BaseAddress > addr ||
                info.RegionSize > ~(ULONG64)0 - info.BaseAddress) break;
            end = info.BaseAddress + info.RegionSize;
            if (end <= addr) break;
            addr = end;
            if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) continue;
            if (!minidump_add_memory64_block(dc, &actual, info.BaseAddress, info.RegionSize)) break;
            descriptor.StartOfMemoryRange = info.BaseAddress;
            descriptor.DataSize = info.RegionSize;
            if (descriptor.DataSize > ~(ULONG64)0 - filepos)
            {
                dc->status = E_OUTOFMEMORY;
                break;
            }
            for (pos = 0; pos < info.RegionSize && !cancel_requested(dc); pos += len)
            {
                len = min(info.RegionSize - pos, 65536);
                if (!dump_read_memory64(dc, info.BaseAddress + pos, buffer, len)) break;
                writeat(dc, filepos + pos, buffer, len);
            }
            if (dc->status != S_OK) break;
            filepos += descriptor.DataSize;
            if (actual.count <= plan.count)
                writeat(dc, descriptors_rva + (actual.count - 1) * sizeof(descriptor),
                        &descriptor, sizeof(descriptor));
        }
        if (dc->status != S_OK) break;
        if (plan.count == actual.count &&
            (!plan.count || !memcmp(plan.blocks, actual.blocks, (SIZE_T)plan.count * sizeof(*plan.blocks))))
        {
            result = 2 * sizeof(ULONG64) + plan.count * sizeof(descriptor);
            break;
        }
    }
    if (!result && dc->status == S_OK) dc->status = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    HeapFree(GetProcessHeap(), 0, buffer);
    HeapFree(GetProcessHeap(), 0, plan.blocks);
    HeapFree(GetProcessHeap(), 0, actual.blocks);
    return result;
}

static DWORD dump_secondary_flags(struct dump_context *dc)
{
    DWORD secondary_flags = 0;

    if (dc->cb)
    {
        MINIDUMP_CALLBACK_INPUT input = {0};
        MINIDUMP_CALLBACK_OUTPUT output = {0};

        input.ProcessId = dc->pid;
        input.ProcessHandle = dc->callback_handle;
        input.CallbackType = SecondaryFlagsCallback;
        if (dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output))
            secondary_flags = output.SecondaryFlags;
    }

    return secondary_flags;
}

static unsigned         dump_misc_info(struct dump_context* dc)
{
    MINIDUMP_MISC_INFO_2 mmi = {0};
    KERNEL_USER_TIMES times;
    SYSTEM_INFO info;
    PROCESSOR_POWER_INFORMATION *power;
    DWORD secondary_flags = dc->snapshot ? dc->secondary_flags : dump_secondary_flags(dc);
    ULONG creation_time;
    NTSTATUS status = STATUS_SUCCESS;

    mmi.SizeOfInfo = sizeof(mmi);
    mmi.Flags1 = MINIDUMP_MISC1_PROCESS_ID;
    mmi.ProcessId = dc->pid;
    if (dc->snapshot)
    {
        memcpy(&times.CreateTime, &dc->snapshot_create_time, sizeof(times.CreateTime));
        memcpy(&times.KernelTime, &dc->snapshot_kernel_time, sizeof(times.KernelTime));
        memcpy(&times.UserTime, &dc->snapshot_user_time, sizeof(times.UserTime));
    }
    else
        status = NtQueryInformationProcess(dc->process->handle, ProcessTimes, &times, sizeof(times), NULL);
    if (!status && RtlTimeToSecondsSince1970(&times.CreateTime, &creation_time))
    {
        mmi.ProcessCreateTime = creation_time;
        mmi.ProcessKernelTime = times.KernelTime.QuadPart / 10000000;
        mmi.ProcessUserTime = times.UserTime.QuadPart / 10000000;
        mmi.Flags1 |= MINIDUMP_MISC1_PROCESS_TIMES;
    }

    if (!(secondary_flags & MiniSecondaryWithoutPowerInfo))
    {
        GetSystemInfo(&info);
        if (info.dwNumberOfProcessors && info.dwNumberOfProcessors <= MAXDWORD / sizeof(*power))
        {
            power = HeapAlloc(GetProcessHeap(), 0, info.dwNumberOfProcessors * sizeof(*power));
            if (!power)
            {
                dc->status = E_OUTOFMEMORY;
                return 0;
            }
            if (!NtPowerInformation(ProcessorInformation, NULL, 0, power,
                                    info.dwNumberOfProcessors * sizeof(*power)))
            {
                mmi.ProcessorMaxMhz = power[0].MaxMhz;
                mmi.ProcessorCurrentMhz = power[0].CurrentMhz;
                mmi.ProcessorMhzLimit = power[0].MhzLimit;
                mmi.ProcessorMaxIdleState = power[0].MaxIdleState;
                mmi.ProcessorCurrentIdleState = power[0].CurrentIdleState;
                mmi.Flags1 |= MINIDUMP_MISC1_PROCESSOR_POWER_INFO;
            }
            HeapFree(GetProcessHeap(), 0, power);
        }
    }

    append(dc, &mmi, sizeof(mmi));
    return sizeof(mmi);
}

struct dump_token_record
{
    MINIDUMP_TOKEN_INFO_HEADER header;
    struct
    {
        ULONG offset;
        ULONG size;
    } fields[7];
};

static BOOL token_sid_size(const void *buffer, ULONG size, PSID sid, ULONG *sid_size)
{
    ULONG_PTR offset = (ULONG_PTR)sid - (ULONG_PTR)buffer;

    if (offset > size || size - offset < FIELD_OFFSET(SID, SubAuthority)) return FALSE;
    *sid_size = RtlLengthSid(sid);
    return *sid_size <= size - offset;
}

static ULONG dump_token(struct dump_context *dc, HANDLE token, DWORD id)
{
    static const TOKEN_INFORMATION_CLASS classes[] = {
        TokenSessionId, TokenUser, TokenGroups, TokenPrimaryGroup, TokenPrivileges,
        TokenStatistics, TokenRestrictedSids
    };
    struct dump_token_record record = {0};
    void *info[ARRAY_SIZE(classes)] = {0};
    ULONG sizes[ARRAY_SIZE(classes)], i, j, sid_size, pos, capacity, total = sizeof(record), result = 0;
    void *buffer;
    TOKEN_GROUPS *groups;
    TOKEN_USER *user;
    TOKEN_PRIMARY_GROUP *primary;
    BYTE *data = NULL, *field;
    ULONG *values;
    NTSTATUS status;

    for (i = 0; i < ARRAY_SIZE(classes); ++i)
    {
        capacity = 0;
        for (;;)
        {
            status = NtQueryInformationToken(token, classes[i], info[i], capacity, &sizes[i]);
            if (!status) break;
            if (status != STATUS_BUFFER_TOO_SMALL || sizes[i] <= capacity) goto done;
            buffer = info[i] ? HeapReAlloc(GetProcessHeap(), 0, info[i], sizes[i]) :
                HeapAlloc(GetProcessHeap(), 0, sizes[i]);
            if (!buffer)
            {
                dc->status = E_OUTOFMEMORY;
                goto done;
            }
            info[i] = buffer;
            capacity = sizes[i];
        }
        record.fields[i].size = sizes[i];
    }
    if (sizes[1] < sizeof(TOKEN_USER) || sizes[3] < sizeof(TOKEN_PRIMARY_GROUP)) goto done;
    user = info[1];
    primary = info[3];
    if (!token_sid_size(user, sizes[1], user->User.Sid, &sid_size)) goto done;
    record.fields[1].size = 2 * sizeof(ULONG) + sid_size;
    if (!token_sid_size(primary, sizes[3], primary->PrimaryGroup, &sid_size)) goto done;
    record.fields[3].size = sizeof(ULONG) + sid_size;
    for (i = 0; i < ARRAY_SIZE(classes); ++i)
    {
        record.fields[i].offset = total;
        if (record.fields[i].size > MAXDWORD - total - 7)
        {
            dc->status = E_OUTOFMEMORY;
            goto done;
        }
        total = (total + record.fields[i].size + 7) & ~7;
    }
    if (!(data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total)))
    {
        dc->status = E_OUTOFMEMORY;
        goto done;
    }
    record.header.TokenSize = total;
    record.header.TokenId = id;
    record.header.TokenHandle = (ULONG_PTR)token;
    memcpy(data, &record, sizeof(record));
    for (i = 0; i < ARRAY_SIZE(classes); ++i)
    {
        field = data + record.fields[i].offset;
        values = (ULONG *)field;
        switch (classes[i])
        {
        case TokenUser:
            values[0] = 2 * sizeof(ULONG);
            values[1] = user->User.Attributes;
            memcpy(field + values[0], user->User.Sid, record.fields[i].size - values[0]);
            break;
        case TokenPrimaryGroup:
            values[0] = (BYTE *)primary->PrimaryGroup - (BYTE *)primary;
            memcpy(field + sizeof(ULONG), primary->PrimaryGroup, record.fields[i].size - sizeof(ULONG));
            break;
        case TokenGroups:
        case TokenRestrictedSids:
            groups = info[i];
            if (sizes[i] < FIELD_OFFSET(TOKEN_GROUPS, Groups) ||
                groups->GroupCount > (sizes[i] - FIELD_OFFSET(TOKEN_GROUPS, Groups)) / sizeof(SID_AND_ATTRIBUTES))
                goto done;
            values[0] = groups->GroupCount;
            pos = (sizeof(ULONG) + groups->GroupCount * 2 * sizeof(ULONG) + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
            if (pos > record.fields[i].size) goto done;
            for (j = 0; j < groups->GroupCount; ++j)
            {
                if (!token_sid_size(groups, sizes[i], groups->Groups[j].Sid, &sid_size) ||
                    sid_size > record.fields[i].size - pos) goto done;
                values[1 + 2 * j] = pos;
                values[2 + 2 * j] = groups->Groups[j].Attributes;
                memcpy(field + pos, groups->Groups[j].Sid, sid_size);
                pos += sid_size;
            }
            break;
        default:
            memcpy(field, info[i], sizes[i]);
            break;
        }
    }
    if (dc->rva > MAXDWORD - total)
    {
        dc->status = E_OUTOFMEMORY;
        goto done;
    }
    append(dc, data, total);
    result = total;
done:
    for (i = 0; i < ARRAY_SIZE(classes); ++i) HeapFree(GetProcessHeap(), 0, info[i]);
    HeapFree(GetProcessHeap(), 0, data);
    return result;
}

static ULONG dump_tokens(struct dump_context *dc)
{
    MINIDUMP_TOKEN_INFO_LIST list = {sizeof(list), 0, sizeof(list), sizeof(struct dump_token_record)};
    ULONG rva = dc->rva, size, i;
    HANDLE token, thread;

    if (dc->rva > MAXDWORD - sizeof(list))
    {
        dc->status = E_OUTOFMEMORY;
        return 0;
    }
    append(dc, &list, sizeof(list));
    if (!NtOpenProcessToken(dc->process->handle, TOKEN_QUERY, &token))
    {
        size = dump_token(dc, token, dc->pid);
        CloseHandle(token);
        if (size) ++list.TokenListEntries;
    }
    for (i = 0; i < dc->num_threads && SUCCEEDED(dc->status); ++i)
    {
        if (!(thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, dc->threads[i].tid))) continue;
        if (!NtOpenThreadToken(thread, TOKEN_QUERY, TRUE, &token))
        {
            size = dump_token(dc, token, dc->threads[i].tid);
            CloseHandle(token);
            if (size) ++list.TokenListEntries;
        }
        CloseHandle(thread);
    }
    list.TokenListSize = dc->rva - rva;
    writeat(dc, rva, &list, sizeof(list));
    return list.TokenListSize;
}

static BOOL initialize_dump_process(struct dump_context *dc)
{
    HANDLE handle = dc->callback_handle;
    DWORD error;

    if (dc->snapshot)
    {
        PSS_PROCESS_INFORMATION process;
        PSS_VA_CLONE_INFORMATION clone;
        PSS_THREAD_INFORMATION threads;

        error = PssQuerySnapshot((HPSS)dc->callback_handle, PSS_QUERY_PROCESS_INFORMATION, &process, sizeof(process));
        if (error) goto failed;
        error = PssQuerySnapshot((HPSS)dc->callback_handle, PSS_QUERY_VA_CLONE_INFORMATION, &clone, sizeof(clone));
        if (error) goto failed;
        error = PssQuerySnapshot((HPSS)dc->callback_handle, PSS_QUERY_THREAD_INFORMATION, &threads, sizeof(threads));
        if (error != ERROR_NOT_FOUND)
        {
            if (error) goto failed;
            if (threads.ThreadsCaptured)
            {
                error = ERROR_NOT_SUPPORTED;
                goto failed;
            }
        }
        if (!DuplicateHandle(GetCurrentProcess(), clone.VaCloneHandle, GetCurrentProcess(),
                             &dc->clone_handle, 0, FALSE, DUPLICATE_SAME_ACCESS))
        {
            error = GetLastError();
            goto failed;
        }
        handle = dc->clone_handle;
        memcpy(&dc->snapshot_create_time, &process.CreateTime, sizeof(dc->snapshot_create_time));
        memcpy(&dc->snapshot_kernel_time, &process.KernelTime, sizeof(dc->snapshot_kernel_time));
        memcpy(&dc->snapshot_user_time, &process.UserTime, sizeof(dc->snapshot_user_time));
    }
    if (!(dc->process = process_find_by_handle(handle)))
    {
        if (!(dc->sym_initialized = SymInitializeW(handle, NULL, FALSE)))
        {
            error = GetLastError();
            goto failed;
        }
        dc->process = process_find_by_handle(handle);
    }
    dc->saved_read_memory = dc->process->read_memory;
    dc->saved_read_memory_user = dc->process->read_memory_user;
    dc->process->read_memory = dump_read_process_memory;
    dc->process->read_memory_user = dc;
    return TRUE;

failed:
    dc->status = HRESULT_FROM_WIN32(error);
    return FALSE;
}

static DWORD CALLBACK write_minidump(void *_args)
{
    struct dump_context *dc = _args;
    static const MINIDUMP_DIRECTORY emptyDir = {UnusedStream, {0, 0}};
    MINIDUMP_HEADER     mdHead;
    MINIDUMP_DIRECTORY  mdDir;
    DWORD               i, nStreams, idx_stream;
    NTSTATUS            status;

    if (dc->impersonation_token && (status = NtSetInformationThread(GetCurrentThread(),
        ThreadImpersonationToken, &dc->impersonation_token, sizeof(dc->impersonation_token))))
    {
        dc->status = HRESULT_FROM_WIN32(RtlNtStatusToDosError(status));
        return FALSE;
    }
    if (dc->cb)
    {
        MINIDUMP_CALLBACK_INPUT input = {0};
        MINIDUMP_CALLBACK_OUTPUT output = {0};

        input.ProcessHandle = dc->callback_handle;
        input.CallbackType = IsProcessSnapshotCallback;
        output.Status = E_NOTIMPL;
        if (dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output) && output.Status == S_FALSE)
            dc->snapshot = TRUE;
        memset(&input, 0, sizeof(input));
        memset(&output, 0, sizeof(output));
        input.ProcessHandle = dc->callback_handle;
        input.CallbackType = VmStartCallback;
        output.Status = E_NOTIMPL;
        if (dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output) && output.Status == S_FALSE)
            dc->callback_vm = TRUE;
        memset(&input, 0, sizeof(input));
        memset(&output, 0, sizeof(output));
        input.CallbackType = IoStartCallback;
        output.Status = E_NOTIMPL;
        if (dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output) && output.Status == S_FALSE)
            dc->callback_io = TRUE;
#ifdef __REACTOS__
        memset(&input, 0, sizeof(input));
        memset(&output, 0, sizeof(output));
        input.CallbackType = WriteKernelMinidumpCallback;
        dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output);
#endif
    }
    if (dc->snapshot) dc->secondary_flags = dump_secondary_flags(dc);
    if (!initialize_dump_process(dc)) return FALSE;
    if (cancel_requested(dc)) return FALSE;
    if (dc->sym_initialized && !module_refresh_list(dc->process))
    {
        dc->status = HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!dc->snapshot && !fetch_process_info(dc)) return FALSE;
    fetch_modules_info(dc);

    /* 1) init */
    nStreams = 7 + (dc->except_param ? 1 : 0) +
        (dc->user_stream ? dc->user_stream->UserStreamCount : 0) +
        ((dc->type & MiniDumpWithThreadInfo) ? 1 : 0) +
        ((dc->type & MiniDumpWithTokenInformation) ? 1 : 0);

    /* pad the directory size to a multiple of 4 for alignment purposes */
    nStreams = (nStreams + 3) & ~3;

    /* 2) write header */
    mdHead.Signature = MINIDUMP_SIGNATURE;
    mdHead.Version = MINIDUMP_VERSION;  /* NOTE: native puts in an 'implementation specific' value in the high order word of this member */
    mdHead.NumberOfStreams = nStreams;
    mdHead.CheckSum = 0;                /* native sets a 0 checksum in its files */
    mdHead.StreamDirectoryRva = sizeof(mdHead);
    mdHead.TimeDateStamp = time(NULL);
    mdHead.Flags = dc->type;
    append(dc, &mdHead, sizeof(mdHead));

    /* 3) write stream directories */
    dc->rva += nStreams * sizeof(mdDir);
    idx_stream = 0;

    /* 3.1) write data stream directories */

    /* must be first in minidump */
    mdDir.StreamType = SystemInfoStream;
    mdDir.Location.Rva = dc->rva;
    mdDir.Location.DataSize = dump_system_info(dc);
    writeat(dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir),
            &mdDir, sizeof(mdDir));

    mdDir.StreamType = ThreadListStream;
    mdDir.Location.Rva = dc->rva;
    mdDir.Location.DataSize = dump_threads(dc);
    writeat(dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir),
            &mdDir, sizeof(mdDir));

    if (dc->type & MiniDumpWithThreadInfo)
    {
        mdDir.StreamType = ThreadInfoListStream;
        mdDir.Location.Rva = dc->rva;
        mdDir.Location.DataSize = dump_thread_info(dc);
        writeat(dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir), &mdDir, sizeof(mdDir));
    }

    if (dc->type & MiniDumpWithTokenInformation)
    {
        mdDir.StreamType = TokenStream;
        mdDir.Location.Rva = dc->rva;
        mdDir.Location.DataSize = dump_tokens(dc);
        writeat(dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir), &mdDir, sizeof(mdDir));
    }

    mdDir.StreamType = ThreadNamesStream;
    mdDir.Location.Rva = dc->rva;
    if ((mdDir.Location.DataSize = dump_threads_names(dc)))
        writeat(dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir),
                &mdDir, sizeof(mdDir));

    mdDir.StreamType = ModuleListStream;
    mdDir.Location.Rva = dc->rva;
    mdDir.Location.DataSize = dump_modules(dc, FALSE);
    writeat(dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir),
            &mdDir, sizeof(mdDir));

    mdDir.StreamType = 0xfff0; /* FIXME: this is part of MS reserved streams */
    mdDir.Location.Rva = dc->rva;
    mdDir.Location.DataSize = dump_modules(dc, TRUE);
    writeat(dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir),
            &mdDir, sizeof(mdDir));


    if (!(dc->type & MiniDumpWithFullMemory))
    {
        fetch_shared_user_data(dc);
        filter_memory(dc);
        mdDir.StreamType = MemoryListStream;
        mdDir.Location.Rva = dc->rva;
        mdDir.Location.DataSize = dump_memory_info(dc);
        writeat(dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir),
                &mdDir, sizeof(mdDir));
    }

    mdDir.StreamType = MiscInfoStream;
    mdDir.Location.Rva = dc->rva;
    mdDir.Location.DataSize = dump_misc_info(dc);
    writeat(dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir),
            &mdDir, sizeof(mdDir));

    /* 3.2) write exception information (if any) */
    if (dc->except_param)
    {
        mdDir.StreamType = ExceptionStream;
        mdDir.Location.Rva = dc->rva;
        mdDir.Location.DataSize = dump_exception_info(dc);
        writeat(dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir),
                &mdDir, sizeof(mdDir));
    }

    /* 3.3) write user defined streams (if any) */
    if (dc->user_stream)
    {
        for (i = 0; i < dc->user_stream->UserStreamCount; i++)
        {
            mdDir.StreamType = dc->user_stream->UserStreamArray[i].Type;
            mdDir.Location.DataSize = dc->user_stream->UserStreamArray[i].BufferSize;
            mdDir.Location.Rva = dc->rva;
            writeat(dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir),
                    &mdDir, sizeof(mdDir));
            append(dc, dc->user_stream->UserStreamArray[i].Buffer,
                   dc->user_stream->UserStreamArray[i].BufferSize);
        }
    }

    /* 3.4) write full memory (if requested) */
    if (dc->type & MiniDumpWithFullMemory)
    {
        mdDir.StreamType = Memory64ListStream;
        mdDir.Location.Rva = dc->rva;
        if (!(mdDir.Location.DataSize = dump_memory64_info(dc))) return FALSE;
        writeat(dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir),
                &mdDir, sizeof(mdDir));
    }

    /* fill the remaining directory entries with 0's (unused stream types) */
    /* NOTE: this should always come last in the dump! */
    for (i = idx_stream; i < nStreams; i++)
        writeat(dc, mdHead.StreamDirectoryRva + i * sizeof(emptyDir), &emptyDir, sizeof(emptyDir));

    if (dc->callback_io)
    {
        MINIDUMP_CALLBACK_INPUT input = {0};
        MINIDUMP_CALLBACK_OUTPUT output = {0};

        input.ProcessId = dc->pid;
        input.ProcessHandle = dc->callback_handle;
        input.CallbackType = IoFinishCallback;
        input.Io.Handle = dc->hFile;
        output.Status = E_FAIL;
        if (!dc->cb->CallbackRoutine(dc->cb->CallbackParam, &input, &output))
            dc->status = E_FAIL;
        else if (output.Status != S_OK)
            dc->status = FAILED(output.Status) ? output.Status : E_FAIL;
    }
    return !cancel_requested(dc);
}

/******************************************************************
 *		MiniDumpWriteDump (DEBUGHLP.@)
 *
 */
BOOL WINAPI MiniDumpWriteDump(HANDLE hProcess, DWORD pid, HANDLE hFile,
                              MINIDUMP_TYPE DumpType,
                              PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
                              PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
                              PMINIDUMP_CALLBACK_INFORMATION CallbackParam)
{
    struct dump_context dc = {0};
    BOOL                ret = FALSE;

    TRACE("(%p, %lu, %p, %u, %p, %p, %p)\n",
          hProcess, pid, hFile, DumpType, ExceptionParam, UserStreamParam, CallbackParam);

    if (DumpType & MiniDumpWithHandleData)
        FIXME("NIY MiniDumpWithHandleData\n");
    if (DumpType & MiniDumpFilterMemory)
        FIXME("NIY MiniDumpFilterMemory\n");
    if (DumpType & MiniDumpScanMemory)
        FIXME("NIY MiniDumpScanMemory\n");

    dc.callback_handle = hProcess;
    dc.hFile = hFile;
    dc.pid = pid;
    dc.writer_tid = GetCurrentThreadId();
    dc.impersonation_token = NULL;
    dc.modules = NULL;
    dc.num_modules = 0;
    dc.alloc_modules = 0;
    dc.threads = NULL;
    dc.num_threads = 0;
    dc.thread_info = NULL;
    dc.cb = CallbackParam;
    dc.callback_io = FALSE;
    dc.callback_vm = FALSE;
    dc.check_cancel = TRUE;
    dc.status = S_OK;
    dc.type = DumpType;
    dc.mem = NULL;
    dc.num_mem = 0;
    dc.alloc_mem = 0;
    dc.rva = 0;
    dc.except_param = (ExceptionParam && ExceptionParam->ExceptionPointers) ? ExceptionParam : NULL;
    dc.user_stream = UserStreamParam;

    /* have a dedicated thread for fetching info on self */
    if (dc.pid != GetCurrentProcessId())
        ret = write_minidump(&dc);
    else
    {
        DWORD  exit_code;
        HANDLE h = NULL;
        NTSTATUS status = NtOpenThreadToken(GetCurrentThread(), TOKEN_IMPERSONATE, TRUE, &dc.impersonation_token);
        if (status && status != STATUS_NO_TOKEN)
            dc.status = HRESULT_FROM_WIN32(RtlNtStatusToDosError(status));
        else
            h = CreateThread(NULL, 0, write_minidump, &dc, 0, NULL);
        if (h)
        {
            if (WaitForSingleObject(h, INFINITE) == WAIT_OBJECT_0 && GetExitCodeThread(h, &exit_code))
                ret = exit_code;
            else
                TerminateThread(h, 0);
            CloseHandle(h);
        }
    }

    if (dc.process)
    {
        dc.process->read_memory = dc.saved_read_memory;
        dc.process->read_memory_user = dc.saved_read_memory_user;
        if (dc.sym_initialized) SymCleanup(dc.process->handle);
    }
    if (dc.clone_handle) CloseHandle(dc.clone_handle);
    if (dc.impersonation_token) CloseHandle(dc.impersonation_token);

    HeapFree(GetProcessHeap(), 0, dc.mem);
    HeapFree(GetProcessHeap(), 0, dc.modules);
    HeapFree(GetProcessHeap(), 0, dc.threads);
    HeapFree(GetProcessHeap(), 0, dc.thread_info);

    if (dc.status != S_OK) SetLastError(dc.status);
    return ret;
}

/******************************************************************
 *		MiniDumpReadDumpStream (DEBUGHLP.@)
 *
 *
 */
BOOL WINAPI MiniDumpReadDumpStream(PVOID base, ULONG str_idx,
                                   PMINIDUMP_DIRECTORY* pdir,
                                   PVOID* stream, ULONG* size)
{
    MINIDUMP_HEADER*    mdHead = base;

    if (mdHead->Signature == MINIDUMP_SIGNATURE)
    {
        MINIDUMP_DIRECTORY* dir;
        DWORD               i;

        dir = (MINIDUMP_DIRECTORY*)((char*)base + mdHead->StreamDirectoryRva);
        for (i = 0; i < mdHead->NumberOfStreams; i++, dir++)
        {
            if (dir->StreamType == str_idx)
            {
                if (pdir) *pdir = dir;
                if (stream) *stream = (char*)base + dir->Location.Rva;
                if (size) *size = dir->Location.DataSize;
                return TRUE;
            }
        }
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}
