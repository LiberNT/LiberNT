# VCRUNTIME140: the MSVC 2015-2022 runtime surface that MSVC-built
# binaries import directly (not through an api-ms contract). Every entry
# forwards to ucrtbase.dll, which implements all of them (the memory and
# string functions in its CRT sources, __CxxFrameHandler3 as its frame
# handler, and __C_specific_handler onward to ntdll).

@ cdecl -arch=!i386 __C_specific_handler(ptr long ptr ptr) ucrtbase.__C_specific_handler
@ cdecl __CxxFrameHandler3(ptr ptr ptr ptr) ucrtbase.__CxxFrameHandler3
@ cdecl __current_exception() ucrtbase.__current_exception
@ cdecl __current_exception_context() ucrtbase.__current_exception_context
@ cdecl memchr(ptr long long) ucrtbase.memchr
@ cdecl memcmp(ptr ptr long) ucrtbase.memcmp
@ cdecl memcpy(ptr ptr long) ucrtbase.memcpy
@ cdecl memmove(ptr ptr long) ucrtbase.memmove
@ cdecl memset(ptr long long) ucrtbase.memset
@ cdecl strchr(str long) ucrtbase.strchr
@ cdecl strrchr(str long) ucrtbase.strrchr
