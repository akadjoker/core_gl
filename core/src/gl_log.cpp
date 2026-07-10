#include "coregl/gl_log.hpp"

#include <cstdarg>
#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace gl
{

// platform sink: where a message ends up when no user callback is installed
static void platformWrite(LogLevel level, const char* message)
{
#if defined(__ANDROID__)
    int prio = (level == LogLevel::ERROR)  ? ANDROID_LOG_ERROR
               : (level == LogLevel::WARN) ? ANDROID_LOG_WARN
                                           : ANDROID_LOG_INFO;
    __android_log_print(prio, "coregl", "%s", message);
#else
    const char* tag = (level == LogLevel::ERROR)  ? "ERROR"
                      : (level == LogLevel::WARN) ? "WARN "
                                                  : "INFO ";
    FILE* out = (level == LogLevel::INFO) ? stdout : stderr;
    fprintf(out, "[coregl %s] %s\n", tag, message);
    fflush(out);
#endif
}

static Log::Callback s_callback = nullptr;
#if defined(NDEBUG)
static LogLevel s_minLevel = LogLevel::WARN;
#else
static LogLevel s_minLevel = LogLevel::INFO;
#endif

static void dispatch(LogLevel level, const char* fmt, va_list args)
{
    if ((unsigned char)level < (unsigned char)s_minLevel) return;
    char message[1024];
    vsnprintf(message, sizeof(message), fmt, args);
    if (s_callback)
        s_callback(level, message);
    else
        platformWrite(level, message);
}

void Log::Info(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    dispatch(LogLevel::INFO, fmt, args);
    va_end(args);
}

void Log::Warn(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    dispatch(LogLevel::WARN, fmt, args);
    va_end(args);
}

void Log::Error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    dispatch(LogLevel::ERROR, fmt, args);
    va_end(args);
}

void Log::SetCallback(Callback cb)
{
    s_callback = cb;
}

void Log::SetMinLevel(LogLevel level)
{
    s_minLevel = level;
}

} // namespace gl
