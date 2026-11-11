/*
 * PROJECT:     LiberNT system libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PERFLIB V2 consumer functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <advapi32.h>

#include <winperf.h>
#include <perflib.h>
#include <wmistr.h>
#include <perflibv2.h>
#include <wine/list.h>

WINE_DEFAULT_DEBUG_CHANNEL(advapi);

#define PERF_QUERY_MAGIC 0x59525150

struct perf_spec
{
    struct list entry;
    GUID guid;
    ULONG counter_id;
    ULONG instance_id;
    WCHAR *name;
};

struct perf_query
{
    ULONG magic;
    struct list entry;
    struct list specs;
};

struct perf_counterset
{
    const PERF_COUNTERSET_INFO *info;
    const PERF_COUNTER_INFO *counters;
};

struct perf_value
{
    ULONG64 value;
    ULONG status;
};

struct perf_instance
{
    ULONG64 address;
    ULONG size;
    ULONG counterset;
    BYTE *block;
    struct perf_value *values;
};

struct perf_provider
{
    const WCHAR *prefix;
    ULONG pid;
    ULONG id;
    ULONG generation;
    HANDLE section;
    const PERFLIBV2_SHARED *shared;
    SIZE_T view_size;
    GUID guid;
    ULONG flags;
    ULONG64 create_time;
    LONG sequence;
    BYTE *counterset_data;
    struct perf_counterset *countersets;
    ULONG counterset_count;
    struct perf_instance *instances;
    ULONG instance_count;
    HANDLE process;
    ULONG status;
    BOOL valid;
    BOOL read;
};

struct perf_system
{
    struct perf_provider *providers;
    ULONG count;
    ULONG capacity;
    WCHAR machine[MAX_COMPUTERNAME_LENGTH + 1];
};

struct perf_buffer
{
    BYTE *data;
    SIZE_T size;
    SIZE_T capacity;
    BOOL failed;
};

static RTL_SRWLOCK perf_lock = RTL_SRWLOCK_INIT;
static struct list perf_queries = LIST_INIT(perf_queries);

static void *perf_alloc( SIZE_T size )
{
    return HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, size );
}

static void perf_free( void *ptr )
{
    HeapFree( GetProcessHeap(), 0, ptr );
}

static ULONG perf_align8( ULONG size )
{
    return (size + 7) & ~7;
}

static BOOL perf_is_local_machine( const WCHAR *machine )
{
    static const COMPUTER_NAME_FORMAT formats[] =
        { ComputerNameNetBIOS, ComputerNameDnsHostname, ComputerNameDnsFullyQualified };
    WCHAR name[256];
    unsigned int i;
    DWORD size;

    if (!machine || !machine[0]) return TRUE;
    while (*machine == '\\') machine++;
    if (!machine[0] || !wcscmp( machine, L"." ) || !_wcsicmp( machine, L"localhost" )) return TRUE;
    for (i = 0; i < ARRAYSIZE(formats); ++i)
    {
        size = ARRAYSIZE(name);
        if (GetComputerNameExW( formats[i], name, &size ) && !_wcsicmp( machine, name )) return TRUE;
    }
    return FALSE;
}

static BOOL perf_buffer_reserve( struct perf_buffer *buffer, SIZE_T size )
{
    SIZE_T capacity;
    BYTE *data;

    if (buffer->failed) return FALSE;
    if (buffer->size + size <= buffer->capacity) return TRUE;
    capacity = max( buffer->capacity * 2, buffer->size + size );
    capacity = max( capacity, 256 );
    if (buffer->data) data = HeapReAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, buffer->data, capacity );
    else data = perf_alloc( capacity );
    if (!data)
    {
        buffer->failed = TRUE;
        return FALSE;
    }
    buffer->data = data;
    buffer->capacity = capacity;
    return TRUE;
}

static SIZE_T perf_buffer_append( struct perf_buffer *buffer, const void *data, SIZE_T size )
{
    SIZE_T offset = buffer->size;

    if (!perf_buffer_reserve( buffer, size )) return offset;
    if (data) memcpy( buffer->data + offset, data, size );
    else memset( buffer->data + offset, 0, size );
    buffer->size += size;
    return offset;
}

static void perf_buffer_pad8( struct perf_buffer *buffer )
{
    if (buffer->size & 7) perf_buffer_append( buffer, NULL, 8 - (buffer->size & 7) );
}

static void *perf_buffer_at( struct perf_buffer *buffer, SIZE_T offset )
{
    return buffer->data + offset;
}

static ULONG perf_copy_out( struct perf_buffer *buffer, void *out, DWORD out_size, DWORD *actual )
{
    if (buffer->failed) return ERROR_OUTOFMEMORY;
    if (buffer->size > MAXDWORD) return ERROR_OUTOFMEMORY;
    *actual = (DWORD)buffer->size;
    if (buffer->size > out_size) return ERROR_NOT_ENOUGH_MEMORY;
    if (buffer->size) memcpy( out, buffer->data, buffer->size );
    return ERROR_SUCCESS;
}

static void perf_release_snapshot( struct perf_provider *provider )
{
    ULONG i;

    for (i = 0; i < provider->instance_count; ++i)
    {
        perf_free( provider->instances[i].block );
        perf_free( provider->instances[i].values );
    }
    perf_free( provider->instances );
    perf_free( provider->countersets );
    perf_free( provider->counterset_data );
    provider->instances = NULL;
    provider->countersets = NULL;
    provider->counterset_data = NULL;
    provider->instance_count = 0;
    provider->counterset_count = 0;
    provider->read = FALSE;
}

static void perf_close_provider( struct perf_provider *provider )
{
    perf_release_snapshot( provider );
    if (provider->shared) UnmapViewOfFile( provider->shared );
    if (provider->section) CloseHandle( provider->section );
    if (provider->process && provider->process != GetCurrentProcess()) CloseHandle( provider->process );
    provider->shared = NULL;
    provider->section = NULL;
    provider->process = NULL;
    provider->valid = FALSE;
}

static void perf_close_system( struct perf_system *sys )
{
    ULONG i;

    for (i = 0; i < sys->count; ++i) perf_close_provider( &sys->providers[i] );
    perf_free( sys->providers );
    sys->providers = NULL;
    sys->count = 0;
}

static BOOL perf_parse_number( const WCHAR **str, ULONG *value )
{
    const WCHAR *p = *str;
    ULONG64 result = 0;

    if (*p < '0' || *p > '9') return FALSE;
    while (*p >= '0' && *p <= '9')
    {
        result = result * 10 + (*p++ - '0');
        if (result > MAXULONG) return FALSE;
    }
    *value = (ULONG)result;
    *str = p;
    return TRUE;
}

static BOOL perf_parse_section_name( const UNICODE_STRING *name, ULONG *pid, ULONG *id, ULONG *generation )
{
    SIZE_T prefix = wcslen( PERFLIBV2_DATA_PREFIX );
    WCHAR buffer[64];
    const WCHAR *p;

    if (name->Length / sizeof(WCHAR) <= prefix || name->Length / sizeof(WCHAR) >= ARRAYSIZE(buffer)) return FALSE;
    memcpy( buffer, name->Buffer, name->Length );
    buffer[name->Length / sizeof(WCHAR)] = 0;
    if (_wcsnicmp( buffer, PERFLIBV2_DATA_PREFIX, prefix )) return FALSE;
    p = buffer + prefix;
    if (!perf_parse_number( &p, pid ) || *p++ != '_') return FALSE;
    if (!perf_parse_number( &p, id ) || *p++ != '_') return FALSE;
    if (!perf_parse_number( &p, generation ) || *p) return FALSE;
    return TRUE;
}

static void perf_add_candidate( struct perf_system *sys, const WCHAR *prefix, ULONG pid, ULONG id, ULONG generation )
{
    struct perf_provider *providers;
    ULONG i;

    for (i = 0; i < sys->count; ++i)
    {
        if (sys->providers[i].pid != pid || sys->providers[i].id != id || sys->providers[i].prefix != prefix)
            continue;
        if (generation > sys->providers[i].generation) sys->providers[i].generation = generation;
        return;
    }
    if (sys->count == sys->capacity)
    {
        ULONG capacity = max( 16, sys->capacity * 2 );

        if (sys->providers)
            providers = HeapReAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sys->providers,
                                     capacity * sizeof(*providers) );
        else
            providers = perf_alloc( capacity * sizeof(*providers) );
        if (!providers) return;
        sys->providers = providers;
        sys->capacity = capacity;
    }
    sys->providers[sys->count].prefix = prefix;
    sys->providers[sys->count].pid = pid;
    sys->providers[sys->count].id = id;
    sys->providers[sys->count].generation = generation;
    sys->count++;
}

static void perf_enum_directory( struct perf_system *sys, const WCHAR *path, const WCHAR *prefix )
{
    OBJECT_DIRECTORY_INFORMATION *info;
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING string;
    ULONG context = 0, length, pid, id, generation;
    BOOLEAN restart = TRUE;
    HANDLE directory;
    NTSTATUS status;
    ULONG size = 1024;
    BYTE *buffer, *grown;

    RtlInitUnicodeString( &string, path );
    InitializeObjectAttributes( &attr, &string, OBJ_CASE_INSENSITIVE, NULL, NULL );
    if (!NT_SUCCESS(NtOpenDirectoryObject( &directory, DIRECTORY_QUERY, &attr ))) return;
    if (!(buffer = perf_alloc( size )))
    {
        NtClose( directory );
        return;
    }
    for (;;)
    {
        length = 0;
        status = NtQueryDirectoryObject( directory, buffer, size, TRUE, restart, &context, &length );
        if (status == STATUS_BUFFER_TOO_SMALL && length > size && length <= 0x10000
            && (grown = HeapReAlloc( GetProcessHeap(), 0, buffer, length )))
        {
            buffer = grown;
            size = length;
            continue;
        }
        restart = FALSE;
        if (status != STATUS_SUCCESS) break;
        info = (OBJECT_DIRECTORY_INFORMATION *)buffer;
        if (info->TypeName.Length == 7 * sizeof(WCHAR) && !_wcsnicmp( info->TypeName.Buffer, L"Section", 7 )
            && perf_parse_section_name( &info->Name, &pid, &id, &generation ))
            perf_add_candidate( sys, prefix, pid, id, generation );
    }
    perf_free( buffer );
    NtClose( directory );
}

static HANDLE perf_open_mapping( const WCHAR *prefix, const WCHAR *base, BOOL write, void **view, SIZE_T *size )
{
    MEMORY_BASIC_INFORMATION mbi;
    WCHAR name[MAX_PATH];
    HANDLE mapping;

    _snwprintf( name, ARRAYSIZE(name), L"%s\\%s", prefix, base );
    if (!(mapping = OpenFileMappingW( write ? FILE_MAP_WRITE : FILE_MAP_READ, FALSE, name ))) return NULL;
    if (!(*view = MapViewOfFile( mapping, write ? FILE_MAP_WRITE : FILE_MAP_READ, 0, 0, 0 )))
    {
        CloseHandle( mapping );
        return NULL;
    }
    if (size)
    {
        if (!VirtualQuery( *view, &mbi, sizeof(mbi) ))
        {
            UnmapViewOfFile( *view );
            CloseHandle( mapping );
            return NULL;
        }
        *size = mbi.RegionSize;
    }
    return mapping;
}

static BOOL perf_open_generation( struct perf_provider *provider )
{
    const PERFLIBV2_SHARED *shared;
    WCHAR base[64];
    SIZE_T size;
    void *view;
    HANDLE section;

    _snwprintf( base, ARRAYSIZE(base), PERFLIBV2_DATA_FORMAT, provider->pid, provider->id, provider->generation );
    if (!(section = perf_open_mapping( provider->prefix, base, FALSE, &view, &size ))) return FALSE;
    shared = view;
    if (size < sizeof(*shared) || shared->Signature != PERFLIBV2_SIGNATURE || shared->Version != PERFLIBV2_VERSION
        || shared->ProcessId != provider->pid || shared->ProviderId != provider->id)
    {
        UnmapViewOfFile( view );
        CloseHandle( section );
        return FALSE;
    }
    if (provider->shared) UnmapViewOfFile( provider->shared );
    if (provider->section) CloseHandle( provider->section );
    provider->section = section;
    provider->shared = shared;
    provider->view_size = size;
    return TRUE;
}

static LONG perf_read_sequence( const PERFLIBV2_SHARED *shared )
{
    LONG sequence;

    MemoryBarrier();
    sequence = *(volatile const LONG *)&shared->Sequence;
    MemoryBarrier();
    return sequence;
}

static BOOL perf_parse_countersets( struct perf_provider *provider, ULONG bytes, ULONG count )
{
    ULONG offset = 0, i, size;
    const PERF_COUNTERSET_INFO *info;

    if (!count) return !bytes;
    if (!(provider->countersets = perf_alloc( count * sizeof(*provider->countersets) ))) return FALSE;
    for (i = 0; i < count; ++i)
    {
        if (bytes - offset < sizeof(PERF_COUNTERSET_INFO)) return FALSE;
        info = (const PERF_COUNTERSET_INFO *)(provider->counterset_data + offset);
        if (!info->NumCounters || info->NumCounters > (bytes - offset - sizeof(*info)) / sizeof(PERF_COUNTER_INFO))
            return FALSE;
        size = sizeof(*info) + info->NumCounters * sizeof(PERF_COUNTER_INFO);
        provider->countersets[i].info = info;
        provider->countersets[i].counters = (const PERF_COUNTER_INFO *)(info + 1);
        offset += size;
    }
    provider->counterset_count = count;
    return offset == bytes;
}

static BOOL perf_snapshot_once( struct perf_provider *provider )
{
    const PERFLIBV2_SHARED *shared = provider->shared;
    const PERFLIBV2_INSTANCE_ENTRY *entries;
    ULONG counterset_count, counterset_offset, counterset_bytes;
    ULONG instance_count, instance_offset, i;
    LONG sequence;

    sequence = perf_read_sequence( shared );
    if (sequence & 1) return FALSE;

    provider->flags = shared->Flags;
    provider->guid = shared->ProviderGuid;
    provider->create_time = shared->ProcessCreateTime;
    counterset_count = shared->CounterSetCount;
    counterset_offset = shared->CounterSetOffset;
    counterset_bytes = shared->CounterSetBytes;
    instance_count = shared->InstanceCount;
    instance_offset = shared->InstanceOffset;

    if (counterset_offset > provider->view_size || counterset_bytes > provider->view_size - counterset_offset)
        return FALSE;
    if (instance_offset > provider->view_size
        || instance_count > (provider->view_size - instance_offset) / sizeof(PERFLIBV2_INSTANCE_ENTRY))
        return FALSE;

    if (counterset_bytes && !(provider->counterset_data = perf_alloc( counterset_bytes ))) return FALSE;
    if (instance_count && !(provider->instances = perf_alloc( instance_count * sizeof(*provider->instances) )))
        return FALSE;
    if (counterset_bytes) memcpy( provider->counterset_data, (const BYTE *)shared + counterset_offset, counterset_bytes );
    entries = (const PERFLIBV2_INSTANCE_ENTRY *)((const BYTE *)shared + instance_offset);
    for (i = 0; i < instance_count; ++i)
    {
        provider->instances[i].address = entries[i].Address;
        provider->instances[i].size = entries[i].Size;
        provider->instances[i].counterset = entries[i].CounterSet;
    }
    provider->instance_count = instance_count;

    if (perf_read_sequence( shared ) != sequence) return FALSE;
    if (!perf_parse_countersets( provider, counterset_bytes, counterset_count )) return FALSE;
    for (i = 0; i < instance_count; ++i)
        if (provider->instances[i].counterset >= provider->counterset_count) return FALSE;
    provider->sequence = sequence;
    return TRUE;
}

static BOOL perf_snapshot_provider( struct perf_provider *provider )
{
    unsigned int tries, hops = 0;

    provider->valid = FALSE;
    for (tries = 0; tries < 256; ++tries)
    {
        perf_release_snapshot( provider );
        if (provider->shared->Successor)
        {
            if (++hops > 16) return FALSE;
            provider->generation = provider->shared->Successor;
            if (!perf_open_generation( provider )) return FALSE;
            continue;
        }
        if (provider->shared->Flags & PERFLIBV2_FLAG_STOPPED) return FALSE;
        if (perf_snapshot_once( provider ))
        {
            provider->valid = TRUE;
            return TRUE;
        }
        if (tries >= 16) Sleep( 0 );
    }
    perf_release_snapshot( provider );
    return FALSE;
}

static void perf_open_system( struct perf_system *sys )
{
    WCHAR path[64];
    DWORD session = 0, size = ARRAYSIZE(sys->machine);
    ULONG i;

    memset( sys, 0, sizeof(*sys) );
    if (!GetComputerNameW( sys->machine, &size )) sys->machine[0] = 0;
    perf_enum_directory( sys, L"\\BaseNamedObjects", L"Global" );
    if (ProcessIdToSessionId( GetCurrentProcessId(), &session ) && session)
    {
        _snwprintf( path, ARRAYSIZE(path), L"\\Sessions\\%lu\\BaseNamedObjects", session );
        perf_enum_directory( sys, path, L"Local" );
    }
    for (i = 0; i < sys->count; ++i)
    {
        if (perf_open_generation( &sys->providers[i] )) perf_snapshot_provider( &sys->providers[i] );
    }
}

static struct perf_counterset *perf_provider_counterset( struct perf_provider *provider, const GUID *guid )
{
    ULONG i;

    if (!provider->valid) return NULL;
    for (i = 0; i < provider->counterset_count; ++i)
        if (IsEqualGUID( &provider->countersets[i].info->CounterSetGuid, guid )) return &provider->countersets[i];
    return NULL;
}

static struct perf_counterset *perf_find_counterset( struct perf_system *sys, const GUID *guid,
                                                    struct perf_provider **owner )
{
    struct perf_counterset *counterset;
    ULONG i;

    for (i = 0; i < sys->count; ++i)
    {
        if (!(counterset = perf_provider_counterset( &sys->providers[i], guid ))) continue;
        if (owner) *owner = &sys->providers[i];
        return counterset;
    }
    return NULL;
}

static const PERF_COUNTER_INFO *perf_find_counter( const struct perf_counterset *counterset, ULONG id, ULONG *index )
{
    ULONG i;

    for (i = 0; i < counterset->info->NumCounters; ++i)
    {
        if (counterset->counters[i].CounterId != id) continue;
        if (index) *index = i;
        return &counterset->counters[i];
    }
    return NULL;
}

static ULONG perf_counter_data_size( const PERF_COUNTER_INFO *counter )
{
    return (counter->Type & PERF_SIZE_LARGE) ? sizeof(ULONG64) : sizeof(ULONG);
}

static BOOL perf_is_multi_instance( const struct perf_counterset *counterset )
{
    return (counterset->info->InstanceType & PERF_COUNTERSET_FLAG_MULTIPLE) != 0;
}

static ULONG perf_open_process( struct perf_provider *provider )
{
    FILETIME create, exit, kernel, user;
    HANDLE process;

    if (provider->process) return ERROR_SUCCESS;
    if (provider->pid == GetCurrentProcessId())
    {
        provider->process = GetCurrentProcess();
        return ERROR_SUCCESS;
    }
    /* FIXME: consumers need PROCESS_VM_READ on the provider process to read its instance blocks */
    if (!(process = OpenProcess( PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, provider->pid )))
        return GetLastError();
    if (!GetProcessTimes( process, &create, &exit, &kernel, &user )
        || (((ULONG64)create.dwHighDateTime << 32) | create.dwLowDateTime) != provider->create_time)
    {
        CloseHandle( process );
        return ERROR_NOT_FOUND;
    }
    provider->process = process;
    return ERROR_SUCCESS;
}

static ULONG perf_read_memory( struct perf_provider *provider, ULONG64 address, void *buffer, SIZE_T size )
{
    SIZE_T read = 0;

    /* TODO: read providers above 4 GB from a 32-bit consumer (NtWow64ReadVirtualMemory64) */
    if ((ULONG64)(ULONG_PTR)address != address) return ERROR_NOT_SUPPORTED;
    if (!ReadProcessMemory( provider->process, (void *)(ULONG_PTR)address, buffer, size, &read ) || read != size)
        return ERROR_PARTIAL_COPY;
    return ERROR_SUCCESS;
}

static ULONG perf_read_instance( struct perf_provider *provider, struct perf_instance *instance )
{
    const struct perf_counterset *counterset = &provider->countersets[instance->counterset];
    const PERF_COUNTERSET_INSTANCE *block;
    ULONG count = counterset->info->NumCounters, i, ret;
    const WCHAR *name;
    ULONG64 slot;

    if (instance->size < sizeof(*block) + count * sizeof(ULONG64)) return ERROR_INVALID_DATA;
    if (!(instance->block = perf_alloc( instance->size ))) return ERROR_OUTOFMEMORY;
    if (!(instance->values = perf_alloc( count * sizeof(*instance->values) ))) return ERROR_OUTOFMEMORY;
    if ((ret = perf_read_memory( provider, instance->address, instance->block, instance->size ))) return ret;

    block = (const PERF_COUNTERSET_INSTANCE *)instance->block;
    if (block->dwSize != instance->size || !IsEqualGUID( &block->CounterSetGuid, &counterset->info->CounterSetGuid ))
        return ERROR_INVALID_DATA;
    if (block->InstanceNameOffset < sizeof(*block) + count * sizeof(ULONG64)
        || block->InstanceNameOffset > instance->size
        || block->InstanceNameSize < sizeof(WCHAR)
        || block->InstanceNameSize > instance->size - block->InstanceNameOffset)
        return ERROR_INVALID_DATA;
    name = (const WCHAR *)(instance->block + block->InstanceNameOffset);
    if (name[block->InstanceNameSize / sizeof(WCHAR) - 1]) return ERROR_INVALID_DATA;

    for (i = 0; i < count; ++i)
    {
        const PERF_COUNTER_INFO *counter = &counterset->counters[i];

        memcpy( &slot, instance->block + sizeof(*block) + i * sizeof(ULONG64), sizeof(slot) );
        if (counter->Attrib & PERF_ATTRIB_BY_REFERENCE)
        {
            if (!slot)
            {
                instance->values[i].status = ERROR_NO_DATA;
                continue;
            }
            instance->values[i].status = perf_read_memory( provider, slot, &instance->values[i].value,
                                                           perf_counter_data_size( counter ) );
            continue;
        }
        if (perf_counter_data_size( counter ) == sizeof(ULONG)) slot = (ULONG)slot;
        instance->values[i].value = slot;
    }
    return ERROR_SUCCESS;
}

static void perf_read_instances( struct perf_provider *provider, const GUID *guid )
{
    unsigned int tries;
    ULONG i, ret;

    for (tries = 0; tries < 16 && provider->valid; ++tries)
    {
        ret = perf_open_process( provider );
        for (i = 0; !ret && i < provider->instance_count; ++i)
        {
            struct perf_instance *instance = &provider->instances[i];

            if (guid && !IsEqualGUID( &provider->countersets[instance->counterset].info->CounterSetGuid, guid ))
                continue;
            ret = perf_read_instance( provider, instance );
        }
        if (perf_read_sequence( provider->shared ) == provider->sequence
            && !(provider->shared->Flags & PERFLIBV2_FLAG_STOPPED))
        {
            provider->status = ret;
            provider->read = TRUE;
            return;
        }
        perf_snapshot_provider( provider );
    }
    provider->status = provider->valid ? ERROR_BUSY : ERROR_WMI_GUID_NOT_FOUND;
}

static const WCHAR *perf_instance_name( const struct perf_instance *instance )
{
    const PERF_COUNTERSET_INSTANCE *block = (const PERF_COUNTERSET_INSTANCE *)instance->block;

    return (const WCHAR *)(instance->block + block->InstanceNameOffset);
}

static ULONG perf_instance_id( const struct perf_instance *instance )
{
    return ((const PERF_COUNTERSET_INSTANCE *)instance->block)->InstanceId;
}

static BOOL perf_send_request( struct perf_system *sys, struct perf_provider *provider, ULONG code,
                               const void *data, ULONG size, ULONG *result )
{
    HANDLE section, request, done, lock;
    PERFLIBV2_CHANNEL *channel;
    WCHAR base[64], name[MAX_PATH];
    DWORD start, elapsed, wait;
    BOOL answered = FALSE;
    LONG sequence;
    void *view;

    if (!(provider->flags & PERFLIBV2_FLAG_CALLBACK) || size > PERFLIBV2_REQUEST_SIZE) return FALSE;

    _snwprintf( base, ARRAYSIZE(base), PERFLIBV2_CHANNEL_FORMAT, provider->pid, provider->id );
    if (!(section = perf_open_mapping( provider->prefix, base, TRUE, &view, NULL ))) return FALSE;
    channel = view;
    _snwprintf( base, ARRAYSIZE(base), PERFLIBV2_REQUEST_FORMAT, provider->pid, provider->id );
    _snwprintf( name, ARRAYSIZE(name), L"%s\\%s", provider->prefix, base );
    request = OpenEventW( EVENT_MODIFY_STATE, FALSE, name );
    _snwprintf( base, ARRAYSIZE(base), PERFLIBV2_DONE_FORMAT, provider->pid, provider->id );
    _snwprintf( name, ARRAYSIZE(name), L"%s\\%s", provider->prefix, base );
    done = OpenEventW( SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name );
    _snwprintf( base, ARRAYSIZE(base), PERFLIBV2_LOCK_FORMAT, provider->pid, provider->id );
    _snwprintf( name, ARRAYSIZE(name), L"%s\\%s", provider->prefix, base );
    lock = OpenMutexW( SYNCHRONIZE | MUTANT_QUERY_STATE, FALSE, name );

    start = GetTickCount();
    if (channel->Signature == PERFLIBV2_CHANNEL_SIGNATURE && request && done && lock)
    {
        wait = WaitForSingleObject( lock, PERFLIBV2_CALLBACK_TIMEOUT );
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED)
        {
            sequence = InterlockedCompareExchange( &channel->RequestSequence, 0, 0 ) + 1;
            if (!sequence) sequence = 1;
            memcpy( channel->Request, data, size );
            channel->RequestCode = code;
            channel->RequestSize = size;
            InterlockedExchange( &channel->RequestSequence, sequence );
            SetEvent( request );
            for (;;)
            {
                if (InterlockedCompareExchange( &channel->ResponseSequence, 0, 0 ) == sequence)
                {
                    *result = channel->ResponseStatus;
                    answered = TRUE;
                    break;
                }
                elapsed = GetTickCount() - start;
                if (elapsed >= PERFLIBV2_CALLBACK_TIMEOUT) break;
                WaitForSingleObject( done, PERFLIBV2_CALLBACK_TIMEOUT - elapsed );
            }
            ReleaseMutex( lock );
        }
    }
    if (!answered) WARN( "Provider %lu.%lu did not answer request %lu.\n", provider->pid, provider->id, code );
    if (lock) CloseHandle( lock );
    if (done) CloseHandle( done );
    if (request) CloseHandle( request );
    UnmapViewOfFile( view );
    CloseHandle( section );
    return answered;
}

static ULONG perf_notify( struct perf_system *sys, struct perf_provider *provider, ULONG code )
{
    ULONG machine = ((ULONG)wcslen( sys->machine ) + 1) * sizeof(WCHAR), result = ERROR_SUCCESS;
    struct perf_buffer buffer = { 0 };
    WNODE_HEADER *wnode;

    if (!(provider->flags & PERFLIBV2_FLAG_CALLBACK)) return ERROR_SUCCESS;
    perf_buffer_append( &buffer, NULL, sizeof(WNODE_HEADER) );
    perf_buffer_append( &buffer, sys->machine, machine );
    if (!buffer.failed)
    {
        wnode = perf_buffer_at( &buffer, 0 );
        wnode->BufferSize = (ULONG)buffer.size;
        if (!perf_send_request( sys, provider, code, buffer.data, (ULONG)buffer.size, &result )) result = ERROR_SUCCESS;
    }
    perf_free( buffer.data );
    return result;
}

static ULONG perf_notify_counter( struct perf_system *sys, struct perf_provider *provider, ULONG code,
                                  const struct perf_spec *spec )
{
    ULONG machine = ((ULONG)wcslen( sys->machine ) + 1) * sizeof(WCHAR), result = ERROR_SUCCESS;
    ULONG name = spec->name ? ((ULONG)wcslen( spec->name ) + 1) * sizeof(WCHAR) : 0;
    struct perf_buffer buffer = { 0 };
    PERF_COUNTER_IDENTITY *identity;
    WNODE_HEADER *wnode;

    if (!(provider->flags & PERFLIBV2_FLAG_CALLBACK)) return ERROR_SUCCESS;
    perf_buffer_append( &buffer, NULL, sizeof(WNODE_HEADER) + sizeof(PERF_COUNTER_IDENTITY) );
    perf_buffer_append( &buffer, sys->machine, machine );
    if (spec->name) perf_buffer_append( &buffer, spec->name, name );
    if (!buffer.failed)
    {
        wnode = perf_buffer_at( &buffer, 0 );
        wnode->BufferSize = (ULONG)buffer.size;
        identity = perf_buffer_at( &buffer, sizeof(WNODE_HEADER) );
        identity->CounterSetGuid = spec->guid;
        identity->BufferSize = sizeof(*identity) + machine + name;
        identity->CounterId = spec->counter_id;
        identity->InstanceId = spec->instance_id;
        identity->MachineOffset = sizeof(*identity);
        identity->NameOffset = spec->name ? sizeof(*identity) + machine : 0;
        if (!perf_send_request( sys, provider, code, buffer.data, (ULONG)buffer.size, &result )) result = ERROR_SUCCESS;
    }
    perf_free( buffer.data );
    return result;
}

static struct perf_query *perf_query_from_handle( HANDLE handle )
{
    struct perf_query *query;

    LIST_FOR_EACH_ENTRY( query, &perf_queries, struct perf_query, entry )
        if (query == handle && query->magic == PERF_QUERY_MAGIC) return query;
    return NULL;
}

static void perf_free_spec( struct perf_spec *spec )
{
    perf_free( spec->name );
    perf_free( spec );
}

static BOOL perf_names_equal( const WCHAR *a, const WCHAR *b )
{
    if (!a || !b) return a == b;
    return !_wcsicmp( a, b );
}

static BOOL perf_has_wildcards( const WCHAR *name )
{
    return wcschr( name, '*' ) || wcschr( name, '?' );
}

static BOOL perf_match_name( const WCHAR *name, const WCHAR *pattern )
{
    const WCHAR *star = NULL, *resume = NULL;

    while (*name)
    {
        if (*pattern == '*')
        {
            star = ++pattern;
            resume = name;
        }
        else if (*pattern == '?' || towupper( *pattern ) == towupper( *name ))
        {
            pattern++;
            name++;
        }
        else if (star)
        {
            pattern = star;
            name = ++resume;
        }
        else return FALSE;
    }
    while (*pattern == '*') pattern++;
    return !*pattern;
}

static ULONG perf_check_identifiers( const PERF_COUNTER_IDENTIFIER *ids, DWORD size )
{
    DWORD offset = 0;

    if (!ids || size < sizeof(PERF_COUNTER_IDENTIFIER)) return ERROR_INVALID_PARAMETER;
    while (offset < size)
    {
        const PERF_COUNTER_IDENTIFIER *id = (const PERF_COUNTER_IDENTIFIER *)((const BYTE *)ids + offset);

        if (size - offset < sizeof(*id) || id->Size < sizeof(*id) || id->Size > size - offset)
            return ERROR_INVALID_PARAMETER;
        offset += id->Size;
    }
    return ERROR_SUCCESS;
}

static ULONG perf_identifier_name( const PERF_COUNTER_IDENTIFIER *id, const WCHAR **name )
{
    const WCHAR *string = (const WCHAR *)(id + 1);
    ULONG count = (id->Size - sizeof(*id)) / sizeof(WCHAR), i;

    *name = NULL;
    if (!count) return ERROR_SUCCESS;
    for (i = 0; i < count; ++i)
        if (!string[i]) break;
    if (i == count) return ERROR_INVALID_PARAMETER;
    if (i) *name = string;
    return ERROR_SUCCESS;
}

static struct perf_instance *perf_find_named_instance( struct perf_system *sys, const GUID *guid,
                                                       const WCHAR *name, ULONG instance_id )
{
    ULONG i, j;

    for (i = 0; i < sys->count; ++i)
    {
        struct perf_provider *provider = &sys->providers[i];

        if (!perf_provider_counterset( provider, guid )) continue;
        if (!provider->read) perf_read_instances( provider, NULL );
        if (!provider->valid || provider->status) continue;
        for (j = 0; j < provider->instance_count; ++j)
        {
            struct perf_instance *instance = &provider->instances[j];

            if (!instance->block
                || !IsEqualGUID( &provider->countersets[instance->counterset].info->CounterSetGuid, guid ))
                continue;
            if (name && !perf_match_name( perf_instance_name( instance ), name )) continue;
            if (instance_id != PERF_WILDCARD_COUNTER && perf_instance_id( instance ) != instance_id) continue;
            return instance;
        }
    }
    return NULL;
}

static ULONG perf_add_spec( struct perf_system *sys, struct perf_query *query, const PERF_COUNTER_IDENTIFIER *id,
                            struct list *added )
{
    struct perf_counterset *counterset;
    struct perf_spec *spec, *other;
    const WCHAR *name;
    ULONG ret, i;

    if ((ret = perf_identifier_name( id, &name ))) return ret;
    if (!(counterset = perf_find_counterset( sys, &id->CounterSetGuid, NULL ))) return ERROR_WMI_GUID_NOT_FOUND;
    if (id->CounterId != PERF_WILDCARD_COUNTER && !perf_find_counter( counterset, id->CounterId, NULL ))
        return ERROR_WMI_ITEMID_NOT_FOUND;
    if (perf_is_multi_instance( counterset ) ? !name : name != NULL) return ERROR_INVALID_PARAMETER;

    LIST_FOR_EACH_ENTRY( other, &query->specs, struct perf_spec, entry )
    {
        if (IsEqualGUID( &other->guid, &id->CounterSetGuid ) && other->counter_id == id->CounterId
            && other->instance_id == id->InstanceId && perf_names_equal( other->name, name ))
            return ERROR_ALREADY_EXISTS;
    }
    LIST_FOR_EACH_ENTRY( other, added, struct perf_spec, entry )
    {
        if (IsEqualGUID( &other->guid, &id->CounterSetGuid ) && other->counter_id == id->CounterId
            && other->instance_id == id->InstanceId && perf_names_equal( other->name, name ))
            return ERROR_ALREADY_EXISTS;
    }

    if (name && !perf_has_wildcards( name )
        && !perf_find_named_instance( sys, &id->CounterSetGuid, name, id->InstanceId ))
        return ERROR_PATH_NOT_FOUND;

    if (!(spec = perf_alloc( sizeof(*spec) ))) return ERROR_OUTOFMEMORY;
    spec->guid = id->CounterSetGuid;
    spec->counter_id = id->CounterId;
    spec->instance_id = id->InstanceId;
    if (name)
    {
        SIZE_T len = wcslen( name ) + 1;

        if (!(spec->name = perf_alloc( len * sizeof(WCHAR) )))
        {
            perf_free( spec );
            return ERROR_OUTOFMEMORY;
        }
        memcpy( spec->name, name, len * sizeof(WCHAR) );
    }

    for (i = 0; i < sys->count; ++i)
    {
        if (!perf_provider_counterset( &sys->providers[i], &spec->guid )) continue;
        if ((ret = perf_notify_counter( sys, &sys->providers[i], PERF_ADD_COUNTER, spec )))
        {
            perf_free_spec( spec );
            return ret;
        }
    }
    list_add_tail( added, &spec->entry );
    return ERROR_SUCCESS;
}

static void perf_notify_remove( struct perf_system *sys, const struct perf_spec *spec )
{
    ULONG i;

    for (i = 0; i < sys->count; ++i)
    {
        if (!perf_provider_counterset( &sys->providers[i], &spec->guid )) continue;
        perf_notify_counter( sys, &sys->providers[i], PERF_REMOVE_COUNTER, spec );
    }
}

ULONG WINAPI PerfOpenQueryHandle( const WCHAR *machine, HANDLE *handle )
{
    struct perf_query *query;

    TRACE( "machine %s, handle %p.\n", debugstr_w(machine), handle );

    if (!handle) return ERROR_INVALID_PARAMETER;
    /* TODO: query remote machines through the [MS-PCQ] PerflibV2 interface */
    if (!perf_is_local_machine( machine )) return ERROR_NOT_SUPPORTED;
    if (!(query = perf_alloc( sizeof(*query) ))) return ERROR_OUTOFMEMORY;
    query->magic = PERF_QUERY_MAGIC;
    list_init( &query->specs );

    RtlAcquireSRWLockExclusive( &perf_lock );
    list_add_tail( &perf_queries, &query->entry );
    RtlReleaseSRWLockExclusive( &perf_lock );

    *handle = query;
    return ERROR_SUCCESS;
}

ULONG WINAPI PerfCloseQueryHandle( HANDLE handle )
{
    struct perf_spec *spec, *next;
    struct perf_query *query;
    struct perf_system sys;
    struct list specs;

    TRACE( "handle %p.\n", handle );

    RtlAcquireSRWLockExclusive( &perf_lock );
    if (!(query = perf_query_from_handle( handle )))
    {
        RtlReleaseSRWLockExclusive( &perf_lock );
        return ERROR_INVALID_HANDLE;
    }
    list_remove( &query->entry );
    query->magic = 0;
    RtlReleaseSRWLockExclusive( &perf_lock );

    list_init( &specs );
    list_move_tail( &specs, &query->specs );
    if (!list_empty( &specs ))
    {
        perf_open_system( &sys );
        LIST_FOR_EACH_ENTRY( spec, &specs, struct perf_spec, entry ) perf_notify_remove( &sys, spec );
        perf_close_system( &sys );
    }
    LIST_FOR_EACH_ENTRY_SAFE( spec, next, &specs, struct perf_spec, entry )
    {
        list_remove( &spec->entry );
        perf_free_spec( spec );
    }
    perf_free( query );
    return ERROR_SUCCESS;
}

ULONG WINAPI PerfAddCounters( HANDLE handle, PERF_COUNTER_IDENTIFIER *ids, DWORD size )
{
    struct perf_query *query;
    struct perf_system sys;
    struct list added;
    DWORD offset;
    ULONG ret;

    TRACE( "handle %p, ids %p, size %lu.\n", handle, ids, size );

    RtlAcquireSRWLockExclusive( &perf_lock );
    if (!perf_query_from_handle( handle ))
    {
        RtlReleaseSRWLockExclusive( &perf_lock );
        return ERROR_INVALID_HANDLE;
    }
    RtlReleaseSRWLockExclusive( &perf_lock );
    if ((ret = perf_check_identifiers( ids, size ))) return ret;

    list_init( &added );
    perf_open_system( &sys );
    RtlAcquireSRWLockExclusive( &perf_lock );
    if (!(query = perf_query_from_handle( handle )))
    {
        RtlReleaseSRWLockExclusive( &perf_lock );
        perf_close_system( &sys );
        return ERROR_INVALID_HANDLE;
    }
    for (offset = 0; offset < size; offset += ((PERF_COUNTER_IDENTIFIER *)((BYTE *)ids + offset))->Size)
    {
        PERF_COUNTER_IDENTIFIER *id = (PERF_COUNTER_IDENTIFIER *)((BYTE *)ids + offset);

        id->Status = perf_add_spec( &sys, query, id, &added );
    }
    list_move_tail( &query->specs, &added );
    RtlReleaseSRWLockExclusive( &perf_lock );
    perf_close_system( &sys );
    return ERROR_SUCCESS;
}

ULONG WINAPI PerfDeleteCounters( HANDLE handle, PERF_COUNTER_IDENTIFIER *ids, DWORD size )
{
    struct perf_spec *spec, *next;
    struct perf_query *query;
    struct perf_system sys;
    struct list removed;
    const WCHAR *name;
    DWORD offset;
    ULONG ret;

    TRACE( "handle %p, ids %p, size %lu.\n", handle, ids, size );

    RtlAcquireSRWLockExclusive( &perf_lock );
    if (!perf_query_from_handle( handle ))
    {
        RtlReleaseSRWLockExclusive( &perf_lock );
        return ERROR_INVALID_HANDLE;
    }
    if ((ret = perf_check_identifiers( ids, size )))
    {
        RtlReleaseSRWLockExclusive( &perf_lock );
        return ret;
    }
    query = perf_query_from_handle( handle );
    list_init( &removed );
    for (offset = 0; offset < size; offset += ((PERF_COUNTER_IDENTIFIER *)((BYTE *)ids + offset))->Size)
    {
        PERF_COUNTER_IDENTIFIER *id = (PERF_COUNTER_IDENTIFIER *)((BYTE *)ids + offset);

        id->Status = ERROR_INVALID_PARAMETER;
        if (perf_identifier_name( id, &name )) continue;
        LIST_FOR_EACH_ENTRY( spec, &query->specs, struct perf_spec, entry )
        {
            if (!IsEqualGUID( &spec->guid, &id->CounterSetGuid ) || spec->counter_id != id->CounterId
                || spec->instance_id != id->InstanceId || !perf_names_equal( spec->name, name ))
                continue;
            list_remove( &spec->entry );
            list_add_tail( &removed, &spec->entry );
            id->Status = ERROR_SUCCESS;
            break;
        }
    }
    RtlReleaseSRWLockExclusive( &perf_lock );

    if (!list_empty( &removed ))
    {
        perf_open_system( &sys );
        LIST_FOR_EACH_ENTRY( spec, &removed, struct perf_spec, entry ) perf_notify_remove( &sys, spec );
        perf_close_system( &sys );
    }
    LIST_FOR_EACH_ENTRY_SAFE( spec, next, &removed, struct perf_spec, entry )
    {
        list_remove( &spec->entry );
        perf_free_spec( spec );
    }
    return ERROR_SUCCESS;
}

ULONG WINAPI PerfQueryCounterInfo( HANDLE handle, PERF_COUNTER_IDENTIFIER *ids, DWORD size, DWORD *actual )
{
    struct perf_buffer buffer = { 0 };
    PERF_COUNTER_IDENTIFIER *id;
    struct perf_query *query;
    struct perf_spec *spec;
    ULONG index = 0, name, ret;
    SIZE_T offset;

    TRACE( "handle %p, ids %p, size %lu, actual %p.\n", handle, ids, size, actual );

    if (!actual || (!ids && size)) return ERROR_INVALID_PARAMETER;

    RtlAcquireSRWLockExclusive( &perf_lock );
    if (!(query = perf_query_from_handle( handle )))
    {
        RtlReleaseSRWLockExclusive( &perf_lock );
        return ERROR_INVALID_HANDLE;
    }
    LIST_FOR_EACH_ENTRY( spec, &query->specs, struct perf_spec, entry )
    {
        name = spec->name ? ((ULONG)wcslen( spec->name ) + 1) * sizeof(WCHAR) : 0;
        offset = perf_buffer_append( &buffer, NULL, sizeof(*id) );
        if (spec->name) perf_buffer_append( &buffer, spec->name, name );
        perf_buffer_pad8( &buffer );
        if (buffer.failed) break;
        id = perf_buffer_at( &buffer, offset );
        id->CounterSetGuid = spec->guid;
        id->Status = ERROR_SUCCESS;
        id->Size = perf_align8( sizeof(*id) + name );
        id->CounterId = spec->counter_id;
        id->InstanceId = spec->instance_id;
        id->Index = index++;
    }
    RtlReleaseSRWLockExclusive( &perf_lock );

    ret = perf_copy_out( &buffer, ids, size, actual );
    perf_free( buffer.data );
    return ret;
}

static void perf_append_counter_data( struct perf_buffer *buffer, const PERF_COUNTER_INFO *counter,
                                      const struct perf_value *value )
{
    PERF_COUNTER_DATA data;
    ULONG data_size = perf_counter_data_size( counter );

    data.dwDataSize = data_size;
    data.dwSize = perf_align8( sizeof(data) + data_size );
    perf_buffer_append( buffer, &data, sizeof(data) );
    perf_buffer_append( buffer, &value->value, data_size );
    perf_buffer_pad8( buffer );
}

static void perf_append_instance_header( struct perf_buffer *buffer, const struct perf_instance *instance )
{
    const WCHAR *name = perf_instance_name( instance );
    ULONG size = ((ULONG)wcslen( name ) + 1) * sizeof(WCHAR);
    PERF_INSTANCE_HEADER header;

    header.Size = perf_align8( sizeof(header) + size );
    header.InstanceId = perf_instance_id( instance );
    perf_buffer_append( buffer, &header, sizeof(header) );
    perf_buffer_append( buffer, name, size );
    perf_buffer_pad8( buffer );
}

static void perf_append_error( struct perf_buffer *buffer, ULONG status )
{
    PERF_COUNTER_HEADER header;

    header.dwStatus = status;
    header.dwType = PERF_ERROR_RETURN;
    header.dwSize = sizeof(header);
    header.Reserved = 0;
    perf_buffer_append( buffer, &header, sizeof(header) );
}

struct perf_match
{
    struct perf_provider *provider;
    struct perf_instance *instance;
};

static ULONG perf_collect_matches( struct perf_system *sys, const struct perf_spec *spec,
                                   const struct perf_counterset *counterset, struct perf_match **matches,
                                   ULONG *count )
{
    ULONG i, j, capacity = 0, status = ERROR_SUCCESS;
    struct perf_match *array = NULL, *grown;

    *count = 0;
    for (i = 0; i < sys->count; ++i)
    {
        struct perf_provider *provider = &sys->providers[i];

        if (!perf_provider_counterset( provider, &spec->guid )) continue;
        if (provider->status)
        {
            status = provider->status;
            continue;
        }
        for (j = 0; j < provider->instance_count; ++j)
        {
            struct perf_instance *instance = &provider->instances[j];

            if (!instance->block
                || !IsEqualGUID( &provider->countersets[instance->counterset].info->CounterSetGuid, &spec->guid ))
                continue;
            if (spec->name && !perf_match_name( perf_instance_name( instance ), spec->name )) continue;
            if (spec->instance_id != PERF_WILDCARD_COUNTER && perf_instance_id( instance ) != spec->instance_id)
                continue;
            if (*count == capacity)
            {
                capacity = max( 8, capacity * 2 );
                grown = array ? HeapReAlloc( GetProcessHeap(), 0, array, capacity * sizeof(*array) )
                              : perf_alloc( capacity * sizeof(*array) );
                if (!grown)
                {
                    perf_free( array );
                    *matches = NULL;
                    *count = 0;
                    return ERROR_OUTOFMEMORY;
                }
                array = grown;
            }
            array[*count].provider = provider;
            array[*count].instance = instance;
            (*count)++;
        }
    }
    *matches = array;
    return *count ? ERROR_SUCCESS : status;
}

static ULONG perf_match_status( const struct perf_match *matches, ULONG count, ULONG counter_index, BOOL all )
{
    ULONG i, j;

    for (i = 0; i < count; ++i)
    {
        const struct perf_counterset *counterset =
            &matches[i].provider->countersets[matches[i].instance->counterset];

        for (j = 0; j < counterset->info->NumCounters; ++j)
        {
            if (!all && j != counter_index) continue;
            if (matches[i].instance->values[j].status) return matches[i].instance->values[j].status;
        }
    }
    return ERROR_SUCCESS;
}

static void perf_append_spec_data( struct perf_system *sys, struct perf_buffer *buffer, const struct perf_spec *spec )
{
    BOOL multi_result, all = spec->counter_id == PERF_WILDCARD_COUNTER;
    struct perf_counterset *counterset;
    struct perf_provider *owner;
    struct perf_match *matches = NULL;
    PERF_COUNTER_HEADER *header;
    PERF_MULTI_INSTANCES *multi;
    PERF_MULTI_COUNTERS *counters;
    ULONG count, counter_index = 0, i, j, status;
    SIZE_T start, offset;

    if (!(counterset = perf_find_counterset( sys, &spec->guid, &owner )))
    {
        perf_append_error( buffer, ERROR_WMI_GUID_NOT_FOUND );
        return;
    }
    if (!all && !perf_find_counter( counterset, spec->counter_id, &counter_index ))
    {
        perf_append_error( buffer, ERROR_WMI_ITEMID_NOT_FOUND );
        return;
    }
    multi_result = perf_is_multi_instance( counterset );
    /* TODO: aggregate counter sets need the aggregate function of each counter from the provider manifest */
    if ((status = perf_collect_matches( sys, spec, counterset, &matches, &count )))
    {
        perf_append_error( buffer, status );
        return;
    }
    if (!multi_result && !count)
    {
        perf_append_error( buffer, ERROR_WMI_INSTANCE_NOT_FOUND );
        return;
    }
    if (!multi_result) count = 1;
    if ((status = perf_match_status( matches, count, counter_index, all )))
    {
        perf_free( matches );
        perf_append_error( buffer, status );
        return;
    }

    start = perf_buffer_append( buffer, NULL, sizeof(*header) );
    if (all)
    {
        offset = perf_buffer_append( buffer, NULL, sizeof(*counters) );
        for (i = 0; i < counterset->info->NumCounters; ++i)
            perf_buffer_append( buffer, &counterset->counters[i].CounterId, sizeof(ULONG) );
        perf_buffer_pad8( buffer );
        if (!buffer->failed)
        {
            counters = perf_buffer_at( buffer, offset );
            counters->dwCounters = counterset->info->NumCounters;
            counters->dwSize = (ULONG)(buffer->size - offset);
        }
    }
    offset = 0;
    if (multi_result) offset = perf_buffer_append( buffer, NULL, sizeof(*multi) );
    for (i = 0; i < count; ++i)
    {
        struct perf_instance *instance = matches[i].instance;
        const struct perf_counterset *own = &matches[i].provider->countersets[instance->counterset];

        if (multi_result) perf_append_instance_header( buffer, instance );
        for (j = 0; j < own->info->NumCounters; ++j)
        {
            if (!all && j != counter_index) continue;
            perf_append_counter_data( buffer, &own->counters[j], &instance->values[j] );
        }
    }
    if (!buffer->failed)
    {
        if (multi_result)
        {
            multi = perf_buffer_at( buffer, offset );
            multi->dwTotalSize = (ULONG)(buffer->size - offset);
            multi->dwInstances = count;
        }
        header = perf_buffer_at( buffer, start );
        header->dwStatus = ERROR_SUCCESS;
        if (multi_result) header->dwType = all ? PERF_COUNTERSET : PERF_MULTIPLE_INSTANCES;
        else header->dwType = all ? PERF_MULTIPLE_COUNTERS : PERF_SINGLE_COUNTER;
        header->dwSize = (ULONG)(buffer->size - start);
    }
    perf_free( matches );
}

static BOOL perf_provider_needed( struct perf_provider *provider, struct perf_spec **specs, ULONG count )
{
    ULONG i;

    for (i = 0; i < count; ++i)
        if (perf_provider_counterset( provider, &specs[i]->guid )) return TRUE;
    return FALSE;
}

ULONG WINAPI PerfQueryCounterData( HANDLE handle, PERF_DATA_HEADER *data, DWORD size, DWORD *actual )
{
    struct perf_buffer buffer = { 0 };
    struct perf_spec **specs = NULL, *spec;
    struct perf_query *query;
    struct perf_system sys;
    PERF_DATA_HEADER *header;
    LARGE_INTEGER counter, frequency;
    ULONG count = 0, i, ret;
    BOOL *started = NULL;
    FILETIME now;

    TRACE( "handle %p, data %p, size %lu, actual %p.\n", handle, data, size, actual );

    if (!actual || (!data && size)) return ERROR_INVALID_PARAMETER;

    RtlAcquireSRWLockExclusive( &perf_lock );
    if (!(query = perf_query_from_handle( handle )))
    {
        RtlReleaseSRWLockExclusive( &perf_lock );
        return ERROR_INVALID_HANDLE;
    }
    count = list_count( &query->specs );
    if (count && !(specs = perf_alloc( count * sizeof(*specs) )))
    {
        RtlReleaseSRWLockExclusive( &perf_lock );
        return ERROR_OUTOFMEMORY;
    }
    i = 0;
    LIST_FOR_EACH_ENTRY( spec, &query->specs, struct perf_spec, entry )
    {
        struct perf_spec *copy = perf_alloc( sizeof(*copy) );

        if (!copy) break;
        *copy = *spec;
        copy->name = NULL;
        if (spec->name && (copy->name = perf_alloc( (wcslen( spec->name ) + 1) * sizeof(WCHAR) )))
            wcscpy( copy->name, spec->name );
        specs[i++] = copy;
    }
    RtlReleaseSRWLockExclusive( &perf_lock );
    count = i;

    perf_open_system( &sys );
    if (sys.count && !(started = perf_alloc( sys.count * sizeof(*started) )))
    {
        perf_close_system( &sys );
        for (i = 0; i < count; ++i) perf_free_spec( specs[i] );
        perf_free( specs );
        return ERROR_OUTOFMEMORY;
    }
    for (i = 0; i < sys.count; ++i)
    {
        struct perf_provider *provider = &sys.providers[i];

        if (!perf_provider_needed( provider, specs, count )) continue;
        started[i] = TRUE;
        if ((ret = perf_notify( &sys, provider, PERF_COLLECT_START ))) provider->status = ret;
    }

    QueryPerformanceFrequency( &frequency );
    QueryPerformanceCounter( &counter );
    GetSystemTimeAsFileTime( &now );
    for (i = 0; i < sys.count; ++i)
    {
        struct perf_provider *provider = &sys.providers[i];
        ULONG status = provider->status;

        if (!started[i] || status) continue;
        perf_snapshot_provider( provider );
        perf_read_instances( provider, NULL );
    }
    for (i = 0; i < sys.count; ++i)
        if (started[i]) perf_notify( &sys, &sys.providers[i], PERF_COLLECT_END );

    perf_buffer_append( &buffer, NULL, sizeof(*header) );
    for (i = 0; i < count; ++i) perf_append_spec_data( &sys, &buffer, specs[i] );
    if (!buffer.failed)
    {
        header = perf_buffer_at( &buffer, 0 );
        header->dwTotalSize = (ULONG)buffer.size;
        header->dwNumCounters = count;
        header->PerfTimeStamp = counter.QuadPart;
        header->PerfFreq = frequency.QuadPart;
        header->PerfTime100NSec = ((LONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
        FileTimeToSystemTime( &now, &header->SystemTime );
    }
    ret = perf_copy_out( &buffer, data, size, actual );

    perf_free( buffer.data );
    perf_free( started );
    perf_close_system( &sys );
    for (i = 0; i < count; ++i) perf_free_spec( specs[i] );
    perf_free( specs );
    return ret;
}

ULONG WINAPI PerfEnumerateCounterSet( const WCHAR *machine, GUID *ids, DWORD count, DWORD *actual )
{
    struct perf_system sys;
    GUID *found = NULL, *grown;
    ULONG total = 0, capacity = 0, i, j, k, ret = ERROR_SUCCESS;

    TRACE( "machine %s, ids %p, count %lu, actual %p.\n", debugstr_w(machine), ids, count, actual );

    if (!actual || (!ids && count)) return ERROR_INVALID_PARAMETER;
    if (!perf_is_local_machine( machine )) return ERROR_NOT_SUPPORTED;

    perf_open_system( &sys );
    for (i = 0; i < sys.count && !ret; ++i)
    {
        struct perf_provider *provider = &sys.providers[i];

        if (!provider->valid) continue;
        for (j = 0; j < provider->counterset_count && !ret; ++j)
        {
            const GUID *guid = &provider->countersets[j].info->CounterSetGuid;

            for (k = 0; k < total; ++k)
                if (IsEqualGUID( &found[k], guid )) break;
            if (k < total) continue;
            if (total == capacity)
            {
                capacity = max( 16, capacity * 2 );
                grown = found ? HeapReAlloc( GetProcessHeap(), 0, found, capacity * sizeof(*found) )
                              : perf_alloc( capacity * sizeof(*found) );
                if (!grown)
                {
                    ret = ERROR_OUTOFMEMORY;
                    break;
                }
                found = grown;
            }
            found[total++] = *guid;
        }
    }
    perf_close_system( &sys );

    if (!ret)
    {
        *actual = total;
        if (total > count) ret = ERROR_NOT_ENOUGH_MEMORY;
        else if (total) memcpy( ids, found, total * sizeof(*found) );
    }
    perf_free( found );
    return ret;
}

ULONG WINAPI PerfEnumerateCounterSetInstances( const WCHAR *machine, const GUID *guid, PERF_INSTANCE_HEADER *instances,
                                               DWORD size, DWORD *actual )
{
    struct perf_counterset *counterset = NULL;
    struct perf_buffer buffer = { 0 };
    struct perf_system sys;
    ULONG i, j, found = 0, registered = 0, ret = ERROR_SUCCESS;
    BOOL multi;

    TRACE( "machine %s, guid %s, instances %p, size %lu, actual %p.\n", debugstr_w(machine), debugstr_guid(guid),
           instances, size, actual );

    if (!guid || !actual || (!instances && size)) return ERROR_INVALID_PARAMETER;
    if (!perf_is_local_machine( machine )) return ERROR_NOT_SUPPORTED;

    perf_open_system( &sys );
    if (!(counterset = perf_find_counterset( &sys, guid, NULL )))
    {
        perf_close_system( &sys );
        return ERROR_WMI_GUID_NOT_FOUND;
    }
    multi = perf_is_multi_instance( counterset );
    for (i = 0; i < sys.count && !ret; ++i)
    {
        if (!perf_provider_counterset( &sys.providers[i], guid )) continue;
        ret = perf_notify( &sys, &sys.providers[i], PERF_ENUM_INSTANCES );
    }
    for (i = 0; i < sys.count && !ret; ++i)
    {
        struct perf_provider *provider = &sys.providers[i];

        if (!perf_provider_counterset( provider, guid )) continue;
        perf_snapshot_provider( provider );
        perf_read_instances( provider, guid );
        if (!perf_provider_counterset( provider, guid )) continue;
        registered++;
        if (provider->status)
        {
            ret = provider->status;
            break;
        }
        for (j = 0; j < provider->instance_count; ++j)
        {
            struct perf_instance *instance = &provider->instances[j];

            if (!instance->block
                || !IsEqualGUID( &provider->countersets[instance->counterset].info->CounterSetGuid, guid ))
                continue;
            perf_append_instance_header( &buffer, instance );
            found++;
        }
    }
    if (!ret && !registered) ret = ERROR_WMI_GUID_NOT_FOUND;
    if (!ret && !found) ret = ERROR_WMI_INSTANCE_NOT_FOUND;
    if (!ret && found > 1 && !multi) ret = ERROR_WMI_INVALID_REGINFO;
    if (!ret) ret = perf_copy_out( &buffer, instances, size, actual );
    perf_free( buffer.data );
    perf_close_system( &sys );
    return ret;
}

ULONG WINAPI PerfQueryCounterSetRegistrationInfo( const WCHAR *machine, const GUID *guid, PerfRegInfoType code,
                                                  DWORD lang, BYTE *info, DWORD size, DWORD *actual )
{
    struct perf_counterset *counterset;
    struct perf_buffer buffer = { 0 };
    struct perf_provider *owner = NULL;
    PERF_COUNTERSET_REG_INFO *set_info;
    PERF_COUNTER_REG_INFO counter_info;
    const PERF_COUNTER_INFO *counter;
    struct perf_system sys;
    ULONG i, ret = ERROR_SUCCESS;

    TRACE( "machine %s, guid %s, code %u, lang %lu, info %p, size %lu, actual %p.\n", debugstr_w(machine),
           debugstr_guid(guid), code, lang, info, size, actual );

    if (!guid || !actual || (!info && size)) return ERROR_INVALID_PARAMETER;
    if (code < PERF_REG_COUNTERSET_STRUCT || code > PERF_REG_COUNTER_ENGLISH_NAMES) return ERROR_INVALID_PARAMETER;
    if (!perf_is_local_machine( machine )) return ERROR_NOT_SUPPORTED;

    perf_open_system( &sys );
    if (!(counterset = perf_find_counterset( &sys, guid, &owner )))
    {
        perf_close_system( &sys );
        return ERROR_WMI_GUID_NOT_FOUND;
    }

    switch (code)
    {
    case PERF_REG_COUNTERSET_STRUCT:
        perf_buffer_append( &buffer, NULL, sizeof(*set_info) );
        for (i = 0; i < counterset->info->NumCounters; ++i) perf_buffer_append( &buffer, NULL, sizeof(counter_info) );
        if (buffer.failed) break;
        set_info = perf_buffer_at( &buffer, 0 );
        set_info->CounterSetGuid = counterset->info->CounterSetGuid;
        set_info->CounterSetType = 0;
        /* FIXME: the counter set detail level comes from its counters, there is no registered value for it */
        set_info->DetailLevel = PERF_DETAIL_WIZARD;
        set_info->NumCounters = counterset->info->NumCounters;
        set_info->InstanceType = counterset->info->InstanceType;
        for (i = 0; i < counterset->info->NumCounters; ++i)
        {
            if (counterset->counters[i].DetailLevel < set_info->DetailLevel)
                set_info->DetailLevel = counterset->counters[i].DetailLevel;
        }
        for (i = 0; i < counterset->info->NumCounters; ++i)
        {
            PERF_COUNTER_REG_INFO *reg = (PERF_COUNTER_REG_INFO *)(set_info + 1) + i;
            const PERF_COUNTER_INFO *source = &counterset->counters[i];

            /* TODO: base, time, frequency and multi counter links and aggregate functions need the provider manifest */
            reg->CounterId = source->CounterId;
            reg->Type = source->Type;
            reg->Attrib = source->Attrib;
            reg->DetailLevel = source->DetailLevel;
            reg->DefaultScale = source->Scale;
            reg->BaseCounterId = PERF_WILDCARD_COUNTER;
            reg->PerfTimeId = PERF_WILDCARD_COUNTER;
            reg->PerfFreqId = PERF_WILDCARD_COUNTER;
            reg->MultiId = PERF_WILDCARD_COUNTER;
            reg->AggregateFunc = PERF_AGGREGATE_UNDEFINED;
            reg->Reserved = 0;
        }
        break;

    case PERF_REG_COUNTER_STRUCT:
        if (!(counter = perf_find_counter( counterset, lang, NULL )))
        {
            ret = ERROR_WMI_ITEMID_NOT_FOUND;
            break;
        }
        memset( &counter_info, 0, sizeof(counter_info) );
        counter_info.CounterId = counter->CounterId;
        counter_info.Type = counter->Type;
        counter_info.Attrib = counter->Attrib;
        counter_info.DetailLevel = counter->DetailLevel;
        counter_info.DefaultScale = counter->Scale;
        counter_info.BaseCounterId = PERF_WILDCARD_COUNTER;
        counter_info.PerfTimeId = PERF_WILDCARD_COUNTER;
        counter_info.PerfFreqId = PERF_WILDCARD_COUNTER;
        counter_info.MultiId = PERF_WILDCARD_COUNTER;
        counter_info.AggregateFunc = PERF_AGGREGATE_UNDEFINED;
        perf_buffer_append( &buffer, &counter_info, sizeof(counter_info) );
        break;

    case PERF_REG_PROVIDER_GUID:
        perf_buffer_append( &buffer, &owner->guid, sizeof(GUID) );
        break;

    default:
        /* TODO: counter set, counter and provider names need manifest registration (lodctr /m) */
        ret = ERROR_NOT_FOUND;
        break;
    }

    if (!ret) ret = perf_copy_out( &buffer, info, size, actual );
    perf_free( buffer.data );
    perf_close_system( &sys );
    return ret;
}
