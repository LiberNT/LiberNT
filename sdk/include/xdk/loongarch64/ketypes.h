
#ifndef _LOONGARCH64_CONTEXT_DEFINED
#define _LOONGARCH64_CONTEXT_DEFINED

#define CONTEXT_LOONGARCH64          0x01000000L
#define CONTEXT_CONTROL             (CONTEXT_LOONGARCH64 | 0x00000001L)
#define CONTEXT_INTEGER             (CONTEXT_LOONGARCH64 | 0x00000002L)
#define CONTEXT_FLOATING_POINT      (CONTEXT_LOONGARCH64 | 0x00000004L)
#define CONTEXT_FULL                (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT)
#define CONTEXT_ALL                 CONTEXT_FULL
#define CONTEXT_EXCEPTION_ACTIVE    0x8000000
#define CONTEXT_SERVICE_ACTIVE      0x10000000
#define CONTEXT_UNWOUND_TO_CALL     0x20000000
#define CONTEXT_EXCEPTION_REQUEST   0x40000000
#define CONTEXT_EXCEPTION_REPORTING 0x80000000

typedef struct DECLSPEC_ALIGN(32) _CONTEXT
{
    ULONG ContextFlags;
    ULONG Fcsr;
    ULONG64 Fcc;
    ULONG64 Pc;
    ULONG64 Reserved;
    union
    {
        ULONG64 R[32];
        struct
        {
            ULONG64 Zero;
            ULONG64 Ra;
            ULONG64 Tp;
            ULONG64 Sp;
            ULONG64 A0;
            ULONG64 A1;
            ULONG64 A2;
            ULONG64 A3;
            ULONG64 A4;
            ULONG64 A5;
            ULONG64 A6;
            ULONG64 A7;
            ULONG64 T0;
            ULONG64 T1;
            ULONG64 T2;
            ULONG64 T3;
            ULONG64 T4;
            ULONG64 T5;
            ULONG64 T6;
            ULONG64 T7;
            ULONG64 T8;
            ULONG64 R21;
            ULONG64 Fp;
            ULONG64 S0;
            ULONG64 S1;
            ULONG64 S2;
            ULONG64 S3;
            ULONG64 S4;
            ULONG64 S5;
            ULONG64 S6;
            ULONG64 S7;
            ULONG64 S8;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
    ULONG64 F[32];
    /* TODO: extend to the 32 x 256-bit vector bank once user mode enables LSX/LASX. */
} CONTEXT, *PCONTEXT;

C_ASSERT(sizeof(CONTEXT) == 0x220);
C_ASSERT(__alignof(CONTEXT) == 32);
C_ASSERT(FIELD_OFFSET(CONTEXT, Fcc) == 0x008);
C_ASSERT(FIELD_OFFSET(CONTEXT, Pc) == 0x010);
#if defined(NONAMELESSUNION)
C_ASSERT(FIELD_OFFSET(CONTEXT, u.R) == 0x020);
#else
C_ASSERT(FIELD_OFFSET(CONTEXT, R) == 0x020);
#endif
C_ASSERT(FIELD_OFFSET(CONTEXT, F) == 0x120);

#endif /* _LOONGARCH64_CONTEXT_DEFINED */
