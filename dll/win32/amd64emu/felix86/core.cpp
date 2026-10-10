/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Run the felix86 recompiler as the CPU core of an NT process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <cstdarg>
#include <cstring>
#include <memory>
#include <new>
#include <sys/mman.h>
#include "biscuit/assembler.hpp"
#include "felix86/common/config.hpp"
#include "felix86/common/frame.hpp"
#include "felix86/common/gdbjit.hpp"
#include "felix86/common/global.hpp"
#include "felix86/common/log.hpp"
#include "felix86/common/perf.hpp"
#include "felix86/common/state.hpp"
#include "felix86/common/utility.hpp"
#include "felix86/common/xsave.hpp"
#include "felix86/emulator.hpp"
#include "felix86/hle/cpuid.hpp"
#include "felix86/hle/mmap.hpp"
#include "felix86/hle/seccomp.hpp"
#include "felix86/hle/thunks.hpp"
#include "felix86/v2/handlers.hpp"
#include "felix86/v2/recompiler.hpp"
#include "bridge.h"

constexpr u32 NT_STATUS_GUARD_PAGE_VIOLATION = 0x80000001;
constexpr u32 NT_STATUS_BREAKPOINT = 0x80000003;
constexpr u32 NT_STATUS_SINGLE_STEP = 0x80000004;
constexpr u32 NT_STATUS_ACCESS_VIOLATION = 0xC0000005;
constexpr u32 NT_STATUS_IN_PAGE_ERROR = 0xC0000006;
constexpr u32 NT_STATUS_ILLEGAL_INSTRUCTION = 0xC000001D;
constexpr u32 NT_STATUS_INTEGER_DIVIDE_BY_ZERO = 0xC0000094;
constexpr u32 NT_STATUS_PRIVILEGED_INSTRUCTION = 0xC0000096;
constexpr u32 NT_STATUS_STACK_OVERFLOW = 0xC00000FD;
constexpr u32 NT_STATUS_ASSERTION_FAILURE = 0xC0000420;

constexpr u64 NT_EXECUTE_FAULT = 8;
constexpr u64 page_size = 4096;

struct NtCpu {
    ThreadState state;
    void* owner = nullptr;
    u64 frame = 0;
    bool compiling = false;
    bool pending = false;
    FELIX86_NT_GUEST pending_fault{};
};

static_assert(offsetof(NtCpu, state) == 0);
static_assert(offsetof(ThreadState, ctx) == 0);
static_assert(offsetof(UserContext, gprs) == 0);
static_assert(offsetof(UserContext, rip) == 16 * sizeof(u64));

Config g_config;
ProcessGlobals g_process_globals;
std::unique_ptr<Mapper> g_mapper;
std::unique_ptr<GDBJIT> g_gdbjit;
std::unordered_map<u64, std::vector<u64>> g_breakpoints;
StartParameters g_params;
u64 g_interpreter_start = 0;
u64 g_interpreter_end = 0;
u64 g_executable_start = 0;
u64 g_executable_end = 0;
bool g_testing = false;
bool g_is_single_thread = false;

#define X(ext) bool Extensions::ext = false;
FELIX86_EXTENSIONS_TOTAL
#undef X
int Extensions::VLEN = 0;

static u32 safepoint_instruction;
static u32 hint_fault_instruction;

static void print(const char* format, va_list arguments) {
    char buffer[1024];
    int length = vsnprintf(buffer, sizeof(buffer) - 2, format, arguments);
    if (length < 0 || length > (int)sizeof(buffer) - 2) {
        length = sizeof(buffer) - 2;
    }

    char* out = buffer;
    for (int i = 0; i < length;) {
        if (buffer[i] == '\x1b') {
            while (i < length && buffer[i] != 'm') {
                i++;
            }
            i++;
        } else {
            *out++ = buffer[i++];
        }
    }
    *out = 0;
    Felix86NtPrint(buffer);
}

void Logger::log(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    print(format, arguments);
    va_end(arguments);
}

void felix86_exit(int code) {
    Felix86NtTerminate(NT_STATUS_ASSERTION_FAILURE);
}

void felix86_assert(const char* file, int line, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    print(format, arguments);
    va_end(arguments);
    Logger::log("%s:%d\n", file, line);
    Felix86NtTerminate(NT_STATUS_ASSERTION_FAILURE);
}

const char* get_version_full() {
    return FELIX86_NT_VERSION;
}

__attribute__((naked)) ThreadState* ThreadState::Get() {
    asm volatile(R"(
        mv a0, gp
        ret
    )");
}

__attribute__((naked)) void ThreadState::Set(ThreadState* state) {
    asm volatile(R"(
        mv gp, a0
        ret
    )");
}

extern "C" void* Felix86NtCompileNext(ThreadState* state, u64 frame) {
    NtCpu* cpu = (NtCpu*)state;
    cpu->frame = frame;
    cpu->compiling = true;
    u64 block = state->recompiler->getCompiledBlock(state, state->GetRip());
    cpu->compiling = false;
    return (void*)block;
}

__attribute__((naked)) void* Emulator::CompileNext(ThreadState* state) {
    asm volatile(R"(
        mv a1, sp
        tail Felix86NtCompileNext
    )");
}

void felix86_syscall(felix86_frame* frame) {
    NtCpu* cpu = (NtCpu*)frame->state;
    Felix86NtSystemService(cpu->owner, cpu->state.ctx.gprs);
}

void felix86_syscall32(felix86_frame* frame, u32 rip_next) {
    ERROR("32-bit system call at %llx", (unsigned long long)frame->state->ctx.rip);
}

void felix86_raise_hardware_breakpoint(ThreadState* state, u64 rip, int index) {
    ERROR("Hardware breakpoint %d at %llx", index, (unsigned long long)rip);
}

void dump_states() {
}

void update_symbols() {
}

std::string get_region(u64 address) {
    return {};
}

bool has_region(u64 address) {
    return true;
}

std::string get_perf_symbol(u64 address) {
    return {};
}

void push_calltrace(ThreadState* state, u64 address) {
}

void pop_calltrace(ThreadState* state) {
}

bool Seccomp::hasFilters() {
    return false;
}

void Seccomp::emitFilters(biscuit::Assembler& as) {
}

void* Thunks::generateTrampoline(Recompiler& rec, const char* name) {
    return nullptr;
}

void* Thunks::generateTrampoline(Recompiler& rec, const char* name, const char* signature, u64 host_ptr) {
    return nullptr;
}

void Thunks::runConstructor(const char* libname, GuestPointers* pointers) {
}

static void set_extensions(u32 mask) {
    Extensions::G = true;
    Extensions::C = true;
    Extensions::Zihintpause = true;
    Extensions::V = mask & FELIX86_NT_EXT_V;
    Extensions::Zba = mask & FELIX86_NT_EXT_ZBA;
    Extensions::Zbb = mask & FELIX86_NT_EXT_ZBB;
    Extensions::Zbs = mask & FELIX86_NT_EXT_ZBS;
    Extensions::Zbc = mask & FELIX86_NT_EXT_ZBC;
    Extensions::Zicond = mask & FELIX86_NT_EXT_ZICOND;
    Extensions::Zfa = mask & FELIX86_NT_EXT_ZFA;
    Extensions::Zacas = mask & FELIX86_NT_EXT_ZACAS;
    Extensions::Zabha = mask & FELIX86_NT_EXT_ZABHA;
    Extensions::Zvbb = mask & FELIX86_NT_EXT_ZVBB;
    Extensions::Zvbc = mask & FELIX86_NT_EXT_ZVBC;
    Extensions::Zvkned = mask & FELIX86_NT_EXT_ZVKNED;
    Extensions::Zvknha = mask & FELIX86_NT_EXT_ZVKNHA;
    Extensions::Zvfhmin = mask & FELIX86_NT_EXT_ZVFHMIN;
    Extensions::Zknd = mask & FELIX86_NT_EXT_ZKND;
    Extensions::Zicbom = mask & FELIX86_NT_EXT_ZICBOM;
    Extensions::Zicclsm = mask & FELIX86_NT_EXT_ZICCLSM;
}

int Felix86NtInitialize(void) {
    Felix86NtRunConstructors();

    g_config.inline_syscalls = false;
    g_config.protect_pages = false;
    g_config.quiet = true;

    set_extensions(Felix86NtProbeExtensions());
    if (!Extensions::V || !Extensions::Zba || !Extensions::Zbb || !Extensions::Zbs) {
        Logger::log("felix86 requires rv64gv_zba_zbb_zbs\n");
        return -1;
    }

    Extensions::VLEN = Felix86NtVectorBytes() * 8;
    if (Extensions::VLEN < 128) {
        Logger::log("felix86 requires a vector length of at least 128 bits\n");
        return -1;
    }

    {
        biscuit::Assembler safepoint((u8*)&safepoint_instruction, sizeof(u32));
        safepoint.SD(x0, -8, Recompiler::threadStatePointer());
        biscuit::Assembler hint((u8*)&hint_fault_instruction, sizeof(u32));
        hint.SD(x0, 0, x0);
    }

    Handlers::initialize();
    g_mapper = std::make_unique<Mapper>();
    return 0;
}

FELIX86_NT_CPU* Felix86NtCreateCpu(void* owner, uint64_t teb) {
    size_t size = page_size + sizeof(NtCpu);
    u8* memory = (u8*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        return nullptr;
    }

    NtCpu* cpu = new (memory + page_size) NtCpu;
    ThreadState* state = &cpu->state;
    cpu->owner = owner;
    state->deferred_fault_page = memory;
    state->ctx.cs = 0x33;
    state->ctx.ds = 0x2b;
    state->ctx.es = 0x2b;
    state->ctx.ss = 0x2b;
    state->ctx.gs = 0x2b;
    state->ctx.fs = 0x53;
    state->ctx.gsbase = teb;
    sigemptyset(&state->signal_mask);
    ThreadState::Set(state);
    state->recompiler = new Recompiler;

    auto lock = g_process_globals.states_lock.lock();
    g_process_globals.states.push_back(state);
    return (FELIX86_NT_CPU*)cpu;
}

void Felix86NtDestroyCpu(FELIX86_NT_CPU* handle) {
    NtCpu* cpu = (NtCpu*)handle;
    ThreadState* state = &cpu->state;
    {
        auto lock = g_process_globals.states_lock.lock();
        auto it = std::find(g_process_globals.states.begin(), g_process_globals.states.end(), state);
        if (it != g_process_globals.states.end()) {
            g_process_globals.states.erase(it);
        }
    }

    delete state->recompiler;
    cpu->~NtCpu();
    munmap((u8*)cpu - page_size, page_size + sizeof(NtCpu));
}

uint64_t* Felix86NtRegisters(FELIX86_NT_CPU* handle) {
    return ((NtCpu*)handle)->state.ctx.gprs;
}

uint64_t Felix86NtGetFlags(FELIX86_NT_CPU* handle) {
    ThreadState* state = &((NtCpu*)handle)->state;
    u64 flags = state->ctx.GetFlags();
    flags |= (u64)state->ac_bit << 18;
    flags |= (u64)state->cpuid_bit << 21;
    return flags;
}

void Felix86NtSetFlags(FELIX86_NT_CPU* handle, uint64_t flags) {
    ThreadState* state = &((NtCpu*)handle)->state;
    state->ctx.SetFlags(flags);
    state->ac_bit = (flags >> 18) & 1;
    state->cpuid_bit = (flags >> 21) & 1;
}

void Felix86NtSaveFloating(FELIX86_NT_CPU* handle, void* area) {
    felix86_fxsave(((NtCpu*)handle)->state.ctx, area);
}

void Felix86NtRestoreFloating(FELIX86_NT_CPU* handle, const void* area) {
    felix86_fxrstor(((NtCpu*)handle)->state.ctx, (void*)area);
}

void Felix86NtRun(FELIX86_NT_CPU* handle) {
    ThreadState* state = &((NtCpu*)handle)->state;
    ThreadState::Set(state);
    state->recompiler->enterDispatcher(state);
    Felix86NtTerminate(NT_STATUS_ASSERTION_FAILURE);
}

void Felix86NtInvalidate(uint64_t start, uint64_t end) {
    Recompiler::invalidateRangeGlobal(start, end, "address space change");
}

void Felix86NtProcessor(uint16_t* level, uint16_t* revision) {
    Cpuid cpuid = felix86_cpuid_impl(1, 0);
    u32 family = (cpuid.eax >> 8) & 0xF;
    u32 model = (cpuid.eax >> 4) & 0xF;
    if (family == 0xF) {
        family += (cpuid.eax >> 20) & 0xFF;
    }
    if (family == 6 || family >= 0xF) {
        model |= ((cpuid.eax >> 16) & 0xF) << 4;
    }
    *level = family;
    *revision = (model << 8) | (cpuid.eax & 0xF);
}

static void jump(FELIX86_NT_NATIVE* native, sigjmp_buf& buffer, u64 value) {
    *native->Pc = buffer->Ra;
    native->X[2] = buffer->Sp;
    native->X[8] = buffer->S[0];
    native->X[9] = buffer->S[1];
    native->F[8] = buffer->Fs[0];
    native->F[9] = buffer->Fs[1];
    for (int i = 0; i < 10; i++) {
        native->X[18 + i] = buffer->S[2 + i];
        native->F[18 + i] = buffer->Fs[2 + i];
    }
    native->X[10] = value;
}

static u64 exact_rip(ThreadState* state, u64 pc, u64 fallback) {
    auto& map = state->recompiler->getHostPcMap();
    auto it = map.lower_bound(pc);
    if (it == map.end()) {
        return fallback;
    }

    BlockMetadata* block = it->second;
    u64 host = block->host_address;
    u64 guest = block->guest_address;
    if (pc < host) {
        return fallback;
    }

    for (auto& sizes : block->translation_sizes) {
        u64 host_end = host + sizes.riscv_instructions_size;
        if (pc < host_end) {
            return guest;
        }
        guest += sizes.x86_instruction_size;
        host = host_end;
    }
    return fallback;
}

static void pull_registers(ThreadState* state, FELIX86_NT_NATIVE* native, u64 rip) {
    u8 vectors[16 * 256];
    u32 vector_bytes = Extensions::VLEN / 8;
    ASSERT(vector_bytes <= 256);
    Felix86NtStoreVectors(vectors, vector_bytes);
    size_t copy = std::min((size_t)vector_bytes, sizeof(XmmReg));
    for (int i = 0; i < 16; i++) {
        memcpy(&state->ctx.xmm[i], vectors + i * vector_bytes, copy);
        state->ctx.gprs[i] = native->X[Recompiler::allocatedGPR((x86_ref_e)(X86_REF_RAX + i)).Index()];
    }

    state->ctx.cf = native->X[Recompiler::allocatedGPR(X86_REF_CF).Index()] != 0;
    state->ctx.zf = native->X[Recompiler::allocatedGPR(X86_REF_ZF).Index()] != 0;
    state->ctx.sf = native->X[Recompiler::allocatedGPR(X86_REF_SF).Index()] != 0;
    state->ctx.of = native->X[Recompiler::allocatedGPR(X86_REF_OF).Index()] != 0;
    state->ctx.rip = rip;
}

static void copy_fault(FELIX86_NT_GUEST* guest, const FELIX86_NT_NATIVE* native) {
    guest->Code = native->Code;
    guest->Parameters = native->Parameters;
    memcpy(guest->Information, native->Information, sizeof(guest->Information));
    guest->Interrupt = FELIX86_NT_NO_INTERRUPT;
}

static void set_fault(FELIX86_NT_GUEST* guest, u32 code, u32 parameters = 0, u64 first = 0, u64 second = 0) {
    guest->Code = code;
    guest->Parameters = parameters;
    guest->Information[0] = first;
    guest->Information[1] = second;
}

static bool hint_fault(ThreadState* state, FELIX86_NT_NATIVE* native, FELIX86_NT_GUEST* guest) {
    u64 pc = *native->Pc;
    u32 hint = *(u32*)(pc + 4);
    if ((hint & 0xFFFFF) != 0x3013) {
        return false;
    }

    u64 ripreg = native->X[Recompiler::allocatedGPR(X86_REF_RIP).Index()];
    u64 rip = exact_rip(state, pc, ripreg);
    switch (hint >> 20) {
    case FELIX86_HINT_DIVZERO:
        set_fault(guest, NT_STATUS_INTEGER_DIVIDE_BY_ZERO);
        break;
    case FELIX86_HINT_INT3:
        set_fault(guest, NT_STATUS_BREAKPOINT, 1);
        break;
    case FELIX86_HINT_INT1:
        set_fault(guest, NT_STATUS_SINGLE_STEP);
        break;
    case FELIX86_HINT_UD2:
        set_fault(guest, NT_STATUS_ILLEGAL_INSTRUCTION);
        break;
    case FELIX86_HINT_GP:
        set_fault(guest, NT_STATUS_PRIVILEGED_INSTRUCTION);
        break;
    case FELIX86_HINT_INT_GP:
        set_fault(guest, NT_STATUS_ACCESS_VIOLATION, 2, 0, -1ull);
        guest->Interrupt = *(u32*)(pc + 8) >> 20;
        break;
    case FELIX86_HINT_TF:
        set_fault(guest, NT_STATUS_SINGLE_STEP);
        rip = ripreg;
        break;
    case FELIX86_HINT_NOT_MAPPED:
    case FELIX86_HINT_NOT_READ:
    case FELIX86_HINT_NOT_EXEC:
        rip = ripreg;
        set_fault(guest, NT_STATUS_ACCESS_VIOLATION, 2, NT_EXECUTE_FAULT, rip);
        break;
    default:
        return false;
    }

    pull_registers(state, native, rip);
    return true;
}

int Felix86NtFault(FELIX86_NT_CPU* handle, FELIX86_NT_NATIVE* native, FELIX86_NT_GUEST* guest) {
    NtCpu* cpu = (NtCpu*)handle;
    ThreadState* state = &cpu->state;
    Recompiler* recompiler = state->recompiler;
    u64 pc = *native->Pc;
    u64 address = native->Parameters >= 2 ? native->Information[1] : 0;
    bool access = native->Code == NT_STATUS_ACCESS_VIOLATION || native->Code == NT_STATUS_IN_PAGE_ERROR ||
                  native->Code == NT_STATUS_GUARD_PAGE_VIOLATION || native->Code == NT_STATUS_STACK_OVERFLOW;

    memset(guest, 0, sizeof(*guest));
    guest->Interrupt = FELIX86_NT_NO_INTERRUPT;
    if (!recompiler) {
        return FELIX86_NT_FAULT_UNRELATED;
    }

    bool in_jit = pc >= (u64)recompiler->getStartOfCodeCache() && pc < (u64)recompiler->getEndOfCodeCache();

    if (state->in_scan_ahead && access && !in_jit && address >= state->scan_ahead_address &&
        address < state->scan_ahead_address + 15) {
        jump(native, state->scan_ahead_buffer, 1);
        return FELIX86_NT_FAULT_RESUME;
    }

    if (state->force_defer_synchronous && access && !in_jit) {
        copy_fault(&cpu->pending_fault, native);
        cpu->pending = true;
        mprotect(state->deferred_fault_page, page_size, PROT_READ);
        jump(native, state->force_defer_buffer, 1);
        return FELIX86_NT_FAULT_RESUME;
    }

    if (native->InService) {
        return FELIX86_NT_FAULT_UNRELATED;
    }

    if (in_jit) {
        u32 instruction;
        memcpy(&instruction, (void*)pc, sizeof(instruction));
        u64 ripreg = native->X[Recompiler::allocatedGPR(X86_REF_RIP).Index()];
        if (cpu->pending && access && instruction == safepoint_instruction && address == (u64)state - 8) {
            mprotect(state->deferred_fault_page, page_size, PROT_READ | PROT_WRITE);
            cpu->pending = false;
            pull_registers(state, native, ripreg);
            *guest = cpu->pending_fault;
            guest->Interrupt = FELIX86_NT_NO_INTERRUPT;
        } else if (access && instruction == hint_fault_instruction && address == 0 && hint_fault(state, native, guest)) {
        } else {
            pull_registers(state, native, exact_rip(state, pc, ripreg));
            copy_fault(guest, native);
        }
    } else {
        if (!cpu->frame || !access || !native->InModule) {
            return FELIX86_NT_FAULT_UNRELATED;
        }

        if (cpu->compiling) {
            cpu->compiling = false;
            state->in_scan_ahead = false;
            state->scan_ahead_address = 0;
            set_fault(guest, native->Code, 2, address == state->ctx.rip ? NT_EXECUTE_FAULT : 0, address);
        } else {
            copy_fault(guest, native);
        }
    }

    guest->Address = state->ctx.rip;
    *native->Pc = recompiler->getRestoreState();
    native->X[2] = cpu->frame;
    native->X[3] = (u64)state;
    return FELIX86_NT_FAULT_GUEST;
}
