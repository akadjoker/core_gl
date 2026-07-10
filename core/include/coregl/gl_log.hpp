#pragma once

namespace gl
{

enum class LogLevel : unsigned char
{
    INFO,
    WARN,
    ERROR
};

// Logging backend. Everything the library reports — shader compile errors,
// incomplete framebuffers, GL debug-output messages — goes through here.
// The default backend writes to the platform's native sink (stderr on
// desktop, logcat on Android, the console on web). Games and editors can
// take over with SetCallback (an in-game console, a file, a network log).
class Log
{
public:
    typedef void (*Callback)(LogLevel level, const char* message);

    // printf-style; message is truncated to 1023 chars
    static void Info(const char* fmt, ...);
    static void Warn(const char* fmt, ...);
    static void Error(const char* fmt, ...);

    // null restores the platform default backend
    static void SetCallback(Callback cb);

    // messages below this level are dropped (default: INFO in debug builds,
    // WARN in release)
    static void SetMinLevel(LogLevel level);
};

} // namespace gl
