//
// (c) 2026 Eduardo Doria.
//

#include "Log.h"
#include "System.h"
#include <stdarg.h>

using namespace doriax;

void Log::print(const char *fmt, ...) {
    va_list arg_ptr;
    va_start(arg_ptr, fmt);
    System::instance().platformLog(0, fmt, arg_ptr);
    va_end(arg_ptr);
}

void Log::verbose(const char *fmt, ...) {
	va_list arg_ptr;
	va_start(arg_ptr, fmt);
    System::instance().platformLog(S_LOG_VERBOSE, fmt, arg_ptr);
	va_end(arg_ptr);
}

void Log::debug(const char *fmt, ...) {
    #ifndef NDEBUG
    va_list arg_ptr;
    va_start(arg_ptr, fmt);
    System::instance().platformLog(S_LOG_DEBUG, fmt, arg_ptr);
    va_end(arg_ptr);
    #endif
}

void Log::warn(const char *fmt, ...) {
    va_list arg_ptr;
    va_start(arg_ptr, fmt);
    System::instance().platformLog(S_LOG_WARN, fmt, arg_ptr);
    va_end(arg_ptr);
}

void Log::error(const char *fmt, ...) {
    va_list arg_ptr;
    va_start(arg_ptr, fmt);
    System::instance().platformLog(S_LOG_ERROR, fmt, arg_ptr);
    va_end(arg_ptr);
}
