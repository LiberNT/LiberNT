
#ifdef _M_IX86
#define SYM(name) _##name
#define IMPSYM(name) __imp__##name
#else
#define SYM(name) name
#define IMPSYM(name) __imp_##name
#endif

#if (defined(_M_IX86) || defined(_M_AMD64))

#include <asm.inc>

MACRO(CREATE_ALIAS1, alias, target)
#ifdef _USE_ML
    EXTERN SYM(&target):PROC
    ALIAS <SYM(&alias)> = <SYM(&target)>
#else
    .weakref SYM(&alias), SYM(&target)
#endif
ENDM

MACRO(CREATE_ALIAS2, alias, target)
#ifdef _USE_ML
    EXTERN IMPSYM(&target):PROC
    ALIAS <IMPSYM(&alias)> = <IMPSYM(&target)>
#else
    .weakref IMPSYM(&alias), IMPSYM(&target)
#endif
ENDM

MACRO(CREATE_ALIAS, alias, target)
    CREATE_ALIAS1 &alias, &target
    CREATE_ALIAS2 &alias, &target
ENDM

#elif defined(_M_ARM64)

.macro CREATE_ALIAS1 alias, target
    .weakref \alias, \target
.endm

.macro CREATE_ALIAS2 alias, target
    .weakref __imp_\alias, __imp_\target
.endm

.macro CREATE_ALIAS alias, target
    CREATE_ALIAS1 \alias, \target
    CREATE_ALIAS2 \alias, \target
.endm
#else
#error "Unsupported platform."
#endif
