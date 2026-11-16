# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The processor set helpers kernel code under test expects, for fixtures of up to 64 processors
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

SHIM = r'''
#define KE_MAXIMUM_GROUPS 1
#define KE_MAXIMUM_PROCESSORS 64
typedef KAFFINITY KI_PROCESSOR_SET[KE_MAXIMUM_GROUPS];
static struct { KAFFINITY Bitmap[KE_MAXIMUM_GROUPS]; } KeActiveProcessors;
__attribute__((unused)) static void KiClearProcessorSet(KAFFINITY *Set) { Set[0] = 0; }
__attribute__((unused)) static int KiIsProcessorInSet(const volatile KAFFINITY *Set, unsigned Number) { return (int)((Set[0] >> Number) & 1); }
__attribute__((unused)) static void KiAddProcessorToSet(KAFFINITY *Set, unsigned Number) { Set[0] |= 1ULL << Number; }
'''
