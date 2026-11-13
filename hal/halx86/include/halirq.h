
#pragma once

#ifdef _MINIHAL_
#define VECTOR2IRQ(vector)	((vector) - PRIMARY_VECTOR_BASE)
#define VECTOR2IRQL(vector)	(PROFILE_LEVEL - VECTOR2IRQ(vector))
#define IRQ2VECTOR(irq)		((irq) + PRIMARY_VECTOR_BASE)
#define HalpVectorToIrq(vector)	((vector) - PRIMARY_VECTOR_BASE)
#define HalpVectorToIrql(vector)	(PROFILE_LEVEL - VECTOR2IRQ(vector))
#define HalpIrqToVector(irq)		((irq) + PRIMARY_VECTOR_BASE)
#else

UCHAR
FASTCALL
HalpIrqToVector(UCHAR Irq);

KIRQL
FASTCALL
HalpVectorToIrql(UCHAR Vector);

UCHAR
FASTCALL
HalpVectorToIrq(UCHAR Vector);

#define VECTOR2IRQ(vector)	HalpVectorToIrq(vector)
#define VECTOR2IRQL(vector)	HalpVectorToIrql(vector)
#define IRQ2VECTOR(irq)		HalpIrqToVector(irq)

#endif

/* Interrupt requirement, kept separate from the controller encoding */
typedef enum _HALP_IRQ_TRIGGER
{
    HalpIrqTriggerUnknown = 0,
    HalpIrqTriggerEdge,
    HalpIrqTriggerLevel
} HALP_IRQ_TRIGGER;

typedef enum _HALP_IRQ_POLARITY
{
    HalpIrqPolarityUnknown = 0,
    HalpIrqPolarityActiveHigh,
    HalpIrqPolarityActiveLow
} HALP_IRQ_POLARITY;

typedef struct _HALP_IRQ_REQUEST
{
    ULONG Vector;
    KIRQL Irql;
    HALP_IRQ_TRIGGER Trigger;
    HALP_IRQ_POLARITY Polarity;
    ULONG ControllerIndex;
    BOOLEAN Msi;
} HALP_IRQ_REQUEST, *PHALP_IRQ_REQUEST;

/* Passed where the call carries no interrupt mode (the disable path) */
#define HALP_IRQ_MODE_UNKNOWN ((KINTERRUPT_MODE)-1)

VOID
HalpBuildInterruptRequest(
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE InterruptMode,
    _Out_ PHALP_IRQ_REQUEST Request);

VOID
HalpValidateInterruptRequest(
    _In_ const HALP_IRQ_REQUEST *Request);
