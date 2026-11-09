//
// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT
// Guard execution of untrusted/user code to prevent process termination on crashes.
// POSIX: catches SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT via sigaction + sigsetjmp/siglongjmp
// Windows: uses SEH (__try/__except) to catch access violations.
//
// NOTE: Recovering from fatal signals/exceptions skips C++ destructors on the stack frames
// between the fault point and the guard boundary. Only use this around untrusted callbacks.
//

#pragma once

#include <functional>

// IMPORTANT: Include platform headers at global scope (not inside any namespace)
// to avoid injecting standard names inside doriax::std, which breaks builds.
#if defined(_WIN32) && defined(_MSC_VER)
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(__EMSCRIPTEN__)
// no special headers
#elif !defined(_WIN32)
    // Use C headers for POSIX signal and setjmp APIs to get C-level names.
    #include <signal.h>
    #include <setjmp.h>
    #include <mutex>
#endif

namespace doriax {

    struct CrashInfo {
        int code;                // signal number or Windows exception code
        const char* name;        // short name
        const char* description; // human-friendly description
    };

#if defined(_WIN32) && defined(_MSC_VER)

    inline const char* winExceptionName(DWORD code) {
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
            case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
            case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
            case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
            case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
            case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
            case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
            default: return "EXCEPTION_UNKNOWN";
        }
    }

    inline const char* winExceptionDesc(DWORD code) {
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION: return "Access violation (likely null or invalid pointer dereference)";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "Array bounds exceeded";
            case EXCEPTION_DATATYPE_MISALIGNMENT: return "Datatype misalignment";
            case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "Floating-point divide by zero";
            case EXCEPTION_FLT_INVALID_OPERATION: return "Invalid floating-point operation";
            case EXCEPTION_INT_DIVIDE_BY_ZERO: return "Integer divide by zero";
            case EXCEPTION_ILLEGAL_INSTRUCTION: return "Illegal instruction";
            case EXCEPTION_STACK_OVERFLOW: return "Stack overflow";
            default: return "Unknown structured exception";
        }
    }

    template<typename Fn>
    inline bool callWithCrashGuard(Fn&& fn, CrashInfo* outInfo) {
        bool ok = false;
        __try {
            fn();
            ok = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (outInfo) {
                DWORD code = GetExceptionCode();
                outInfo->code = static_cast<int>(code);
                outInfo->name = winExceptionName(code);
                outInfo->description = winExceptionDesc(code);
            }
            ok = false;
        }
        return ok;
    }

#elif defined(__EMSCRIPTEN__)

    // Emscripten/WASM does not support POSIX signals or SEH. Just execute directly.
    template<typename Fn>
    inline bool callWithCrashGuard(Fn&& fn, CrashInfo* /*outInfo*/) {
        fn();
        return true;
    }

#elif !defined(_WIN32)
    // POSIX implementation

    namespace detail {
        struct GuardTLS {
            sigjmp_buf* env = nullptr; // innermost guard
            int lastSig = 0;
            unsigned int calls = 0;
        };

        // One TLS state per thread
        inline thread_local GuardTLS g_tls;

        inline const char* sigName(int sig) {
            switch (sig) {
                case SIGSEGV: return "SIGSEGV";
                case SIGBUS:  return "SIGBUS";
                case SIGFPE:  return "SIGFPE";
                case SIGILL:  return "SIGILL";
                case SIGABRT: return "SIGABRT";
                default:      return "SIGNAL";
            }
        }

        inline const char* sigDesc(int sig) {
            switch (sig) {
                case SIGSEGV: return "Segmentation fault (invalid memory reference)";
                case SIGBUS:  return "Bus error (misaligned or invalid access)";
                case SIGFPE:  return "Floating-point exception";
                case SIGILL:  return "Illegal instruction";
                case SIGABRT: return "Abort signal";
                default:      return "Fatal signal";
            }
        }

        inline const int guardedSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
        inline struct sigaction previousActions[5];
        inline std::mutex handlersMutex;

        inline void crashSignalHandler(int sig, siginfo_t* /*info*/, void* /*ucontext*/) {
            if (g_tls.env) {
                g_tls.lastSig = sig;
                siglongjmp(*g_tls.env, 1);
            } else {
                // Not guarded: restore the previous handler and re-raise
                for (int i = 0; i < 5; i++) {
                    if (guardedSignals[i] == sig)
                        sigaction(sig, &previousActions[i], nullptr);
                }
                ::raise(sig);
            }
        }

        inline bool handlersInstalled() {
            struct sigaction current{};
            sigaction(SIGSEGV, nullptr, &current);
            return current.sa_sigaction == &crashSignalHandler;
        }

        // Installed once, not around each call
        inline void installHandlers() {
            std::lock_guard<std::mutex> lock(handlersMutex);
            if (handlersInstalled())
                return;

            struct sigaction sa{};
            sa.sa_sigaction = &crashSignalHandler;
            // Nothing blocked in the handler, so the jump out keeps the signal mask
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = SA_SIGINFO | SA_NODEFER;

            for (int i = 0; i < 5; i++)
                sigaction(guardedSignals[i], &sa, &previousActions[i]);
        }
    } // namespace detail

    template<typename Fn>
    inline bool callWithCrashGuard(Fn&& fn, CrashInfo* outInfo) {
        using namespace detail;

        // Checked again now and then, in case another handler took the signals
        if ((g_tls.calls++ & 1023) == 0 && !handlersInstalled())
            installHandlers();

        sigjmp_buf env;
        sigjmp_buf* outerEnv = g_tls.env;
        g_tls.env = &env;
        g_tls.lastSig = 0;

        if (sigsetjmp(env, 0) == 0) {
            // Execute the user function
            fn();

            // Success path
            g_tls.env = outerEnv;
            return true;
        } else {
            // We jumped back from a signal
            if (outInfo) {
                outInfo->code = g_tls.lastSig;
                outInfo->name = sigName(g_tls.lastSig);
                outInfo->description = sigDesc(g_tls.lastSig);
            }

            g_tls.env = outerEnv;
            return false;
        }
    }

#else
    // Windows with non-MSVC compilers (e.g., MinGW/clang-mingw):
    // SEH __try/__except is not available, and POSIX sigaction isn't reliable on Windows.
    // Fall back to direct execution (no crash guard).
    template<typename Fn>
    inline bool callWithCrashGuard(Fn&& fn, CrashInfo* /*outInfo*/) {
        fn();
        return true;
    }
#endif

} // namespace doriax