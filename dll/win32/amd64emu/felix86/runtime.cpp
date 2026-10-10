/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     C++ runtime entry points of the felix86 sources without a C++ ABI library
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <cstdarg>
#include <cstdlib>
#include <exception>
#include <new>
#include "bridge.h"

constexpr uint32_t NT_STATUS_NO_MEMORY = 0xC0000017;
constexpr uint32_t NT_STATUS_FATAL_APP_EXIT = 0x40000015;

extern "C" {

typedef void (*constructor)(void);
extern constructor __CTOR_LIST__[];

void Felix86NtRunConstructors(void) {
    size_t count = 0;
    while (__CTOR_LIST__[count + 1]) {
        count++;
    }
    for (size_t i = count; i >= 1; i--) {
        __CTOR_LIST__[i]();
    }
}

int __cxa_guard_acquire(uint64_t* guard) {
    uint8_t* state = (uint8_t*)guard;
    for (;;) {
        uint8_t expected = 0;
        if (__atomic_load_n(&state[0], __ATOMIC_ACQUIRE)) {
            return 0;
        }
        if (__atomic_compare_exchange_n(&state[1], &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            if (!__atomic_load_n(&state[0], __ATOMIC_ACQUIRE)) {
                return 1;
            }
            __atomic_store_n(&state[1], 0, __ATOMIC_RELEASE);
            return 0;
        }
        Felix86NtYield();
    }
}

void __cxa_guard_release(uint64_t* guard) {
    uint8_t* state = (uint8_t*)guard;
    __atomic_store_n(&state[0], 1, __ATOMIC_RELEASE);
    __atomic_store_n(&state[1], 0, __ATOMIC_RELEASE);
}

void __cxa_guard_abort(uint64_t* guard) {
    uint8_t* state = (uint8_t*)guard;
    __atomic_store_n(&state[1], 0, __ATOMIC_RELEASE);
}

void __cxa_pure_virtual(void) {
    Felix86NtTerminate(NT_STATUS_FATAL_APP_EXIT);
}

void* __cxa_allocate_exception(size_t size) {
    Felix86NtTerminate(NT_STATUS_FATAL_APP_EXIT);
}

void __cxa_free_exception(void* exception) {
}

void __cxa_throw(void* exception, void* type, void (*destructor)(void*)) {
    Felix86NtTerminate(NT_STATUS_FATAL_APP_EXIT);
}

void* __cxa_begin_catch(void* exception) {
    Felix86NtTerminate(NT_STATUS_FATAL_APP_EXIT);
}

void __cxa_end_catch(void) {
}

void __gxx_personality_seh0(void) {
    Felix86NtTerminate(NT_STATUS_FATAL_APP_EXIT);
}

void _Unwind_Resume(void* exception) {
    Felix86NtTerminate(NT_STATUS_FATAL_APP_EXIT);
}

}

namespace std {

void terminate() noexcept {
    Felix86NtTerminate(NT_STATUS_FATAL_APP_EXIT);
}

void __throw_bad_alloc() {
    Felix86NtTerminate(NT_STATUS_NO_MEMORY);
}

}

_LIBCPP_BEGIN_NAMESPACE_STD
_LIBCPP_BEGIN_EXPLICIT_ABI_ANNOTATIONS

void __libcpp_verbose_abort(const char* format, ...) noexcept {
    Felix86NtPrint(format);
    Felix86NtTerminate(NT_STATUS_FATAL_APP_EXIT);
}

_LIBCPP_END_EXPLICIT_ABI_ANNOTATIONS
_LIBCPP_END_NAMESPACE_STD

static void* allocate(size_t size) {
    void* block = malloc(size ? size : 1);
    if (!block) {
        Felix86NtTerminate(NT_STATUS_NO_MEMORY);
    }
    return block;
}

static void* allocate_aligned(size_t size, size_t alignment) {
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }
    void* raw = allocate(size + alignment + sizeof(void*));
    uintptr_t aligned = ((uintptr_t)raw + sizeof(void*) + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((void**)aligned)[-1] = raw;
    return (void*)aligned;
}

static void free_aligned(void* block) {
    if (block) {
        free(((void**)block)[-1]);
    }
}

void* operator new(size_t size) {
    return allocate(size);
}

void* operator new[](size_t size) {
    return allocate(size);
}

void* operator new(size_t size, const std::nothrow_t&) noexcept {
    return malloc(size ? size : 1);
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept {
    return malloc(size ? size : 1);
}

void* operator new(size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, (size_t)alignment);
}

void* operator new[](size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, (size_t)alignment);
}

void operator delete(void* block) noexcept {
    free(block);
}

void operator delete[](void* block) noexcept {
    free(block);
}

void operator delete(void* block, size_t) noexcept {
    free(block);
}

void operator delete[](void* block, size_t) noexcept {
    free(block);
}

void operator delete(void* block, const std::nothrow_t&) noexcept {
    free(block);
}

void operator delete[](void* block, const std::nothrow_t&) noexcept {
    free(block);
}

void operator delete(void* block, std::align_val_t) noexcept {
    free_aligned(block);
}

void operator delete[](void* block, std::align_val_t) noexcept {
    free_aligned(block);
}

void operator delete(void* block, size_t, std::align_val_t) noexcept {
    free_aligned(block);
}

void operator delete[](void* block, size_t, std::align_val_t) noexcept {
    free_aligned(block);
}
