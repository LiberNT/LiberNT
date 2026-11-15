/*

#define GENERIC_ACCESS                                  \
    (GENERIC_READ    |                                  \
     GENERIC_WRITE   |                                  \
     GENERIC_EXECUTE |                                  \
     GENERIC_ALL)

#define OBJ_PROTECT_CLOSE                               0x01
#define OBJ_AUDIT_OBJECT_CLOSE                          0x04
#define OBJ_HANDLE_ATTRIBUTES                           (OBJ_PROTECT_CLOSE |\
                                                         OBJ_INHERIT |      \
                                                         OBJ_AUDIT_OBJECT_CLOSE)

#define ObpAccessProtectCloseBit 0x02000000L

#define OB_HANDLE_EXCEPTIONS_ENABLED 0x2

#ifdef _WIN64
#define OBP_HANDLE_REFERENCE_INCREMENT 0x8000UL
#else
#define OBP_HANDLE_REFERENCE_INCREMENT 0x20UL
#endif

#ifdef _WIN64
#define KERNEL_HANDLE_FLAG 0xFFFFFFFF80000000ULL
#else
#define KERNEL_HANDLE_FLAG 0x80000000
#endif
#define ObpIsKernelHandle(Handle, ProcessorMode)        \
    ((((ULONG_PTR)(Handle) & KERNEL_HANDLE_FLAG) == KERNEL_HANDLE_FLAG) && \
     ((ProcessorMode) == KernelMode) && \
     ((Handle) != NtCurrentProcess()) && \
     ((Handle) != NtCurrentThread()))

#define ObKernelHandleToHandle(Handle)                  \
    (HANDLE)((ULONG_PTR)(Handle) & ~KERNEL_HANDLE_FLAG)
#define ObMarkHandleAsKernelHandle(Handle)              \
    (HANDLE)((ULONG_PTR)(Handle) | KERNEL_HANDLE_FLAG)

#define ObpGetHandleObject(x)                           \
    ((POBJECT_HEADER)((ULONG_PTR)x->Object & ~OBJ_HANDLE_ATTRIBUTES))

#define ObpGetHeaderForSd(x) \
    CONTAINING_RECORD((x), SECURITY_DESCRIPTOR_HEADER, SecurityDescriptor)

#define ObpGetHeaderForEntry(x) \
    CONTAINING_RECORD((x), SECURITY_DESCRIPTOR_HEADER, Link)

typedef struct _OBP_SET_HANDLE_ATTRIBUTES_CONTEXT
{
    KPROCESSOR_MODE PreviousMode;
    OBJECT_HANDLE_ATTRIBUTE_INFORMATION Information;
} OBP_SET_HANDLE_ATTRIBUTES_CONTEXT, *POBP_SET_HANDLE_ATTRIBUTES_CONTEXT;

typedef struct _OBP_FIND_HANDLE_DATA
{
    POBJECT_HEADER ObjectHeader;
    POBJECT_TYPE ObjectType;
    POBJECT_HANDLE_INFORMATION HandleInformation;
} OBP_FIND_HANDLE_DATA, *POBP_FIND_HANDLE_DATA;

typedef struct _SECURITY_DESCRIPTOR_HEADER
{
    LIST_ENTRY Link;
    ULONG RefCount;
    ULONG FullHash;
    QUAD DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) SecurityDescriptor;
} SECURITY_DESCRIPTOR_HEADER, *PSECURITY_DESCRIPTOR_HEADER;

C_ASSERT((FIELD_OFFSET(SECURITY_DESCRIPTOR_HEADER, SecurityDescriptor) & (MEMORY_ALLOCATION_ALIGNMENT - 1)) == 0);

typedef struct _OB_SD_CACHE_LIST
{
    EX_PUSH_LOCK PushLock;
    LIST_ENTRY Head;
} OB_SD_CACHE_LIST, *POB_SD_CACHE_LIST;

typedef union
{
    WCHAR Name[sizeof(ULARGE_INTEGER) / sizeof(WCHAR)];
    ULARGE_INTEGER Alignment;
} ALIGNEDNAME;

#define TAG_OB_TEMP_STORAGE 'tSbO'
typedef struct _OB_TEMP_BUFFER
{
    ACCESS_STATE LocalAccessState;
    OBJECT_CREATE_INFORMATION ObjectCreateInfo;
    OBP_LOOKUP_CONTEXT LookupContext;
    AUX_ACCESS_DATA AuxData;
} OB_TEMP_BUFFER, *POB_TEMP_BUFFER;

NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

NTAPI

NTSTATUS
NTAPI

NTSTATUS
NTAPI

NTAPI

VOID

NTAPI

NTAPI

VOID
NTAPI

BOOLEAN
NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

NTSTATUS
NTAPI
NTAPI



NTAPI

BOOLEAN
NTAPI

VOID

VOID
NTAPI

NTAPI

NTSTATUS
NTAPI

VOID
NTAPI

VOID
NTAPI
ObpFreeObjectNameBuffer(

VOID
NTAPI

NTSTATUS
NTAPI
NTAPI

NTSTATUS
NTAPI

NTAPI

VOID
NTAPI

VOID
FASTCALL

VOID

NTSTATUS
NTAPI

NTAPI

NTAPI

CODE_SEG("INIT")
NTSTATUS
NTAPI

PSECURITY_DESCRIPTOR
NTAPI
ObpReferenceSecurityDescriptor(

NTAPI

BOOLEAN
NTAPI
ObCheckCreateObjectAccess(

BOOLEAN
NTAPI

NTSTATUS
NTAPI
ObDeassignSecurity(

NTSTATUS
NTAPI
ObQuerySecurityDescriptorInfo(

NTSTATUS
NTAPI
ObSetSecurityDescriptorInfo(

NTAPI

BOOLEAN
NTAPI

NTAPI

NTAPI

VOID
NTAPI

VOID
NTAPI

NTAPI

NTAPI

#include "ob_x.h"
