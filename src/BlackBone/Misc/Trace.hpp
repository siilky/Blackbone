#pragma once

#include <cstdio>
#include <cstdlib>

#pragma warning(push)
#pragma warning(disable : 4091)
#include <DbgHelp.h>
#pragma warning(pop)

namespace blackbone
{
#ifndef BLACKBONE_NO_TRACE

    class FileWriter
    {
    public:
        FileWriter()
        {
            char buf[2048];

            GetTempFileNameA("f:\\temp\\", "bb", GetCurrentProcessId(), buf);
            auto r = fopen_s(&stream, buf, "wb");
            if (r != 0)
            {
                stream = nullptr;

                sprintf_s(buf, "FileQriter: %i\r\n", r);
                OutputDebugStringA(buf);
            }
        }

        static FileWriter * instance()
        {
            static FileWriter w;
            return &w;
        }

         FILE* file() const
        {
            return stream;
        }

        bool isOk() const
        {
            return stream != nullptr;
        }

    private:
        FILE *stream;
    };

inline void DoTraceV( const char* fmt, va_list va_args )
{
    char buf[2048], userbuf[1024];
    vsprintf_s( userbuf, fmt, va_args );
    sprintf_s( buf, "BlackBone: %s\r\n", userbuf );
    OutputDebugStringA( buf );

#ifdef CONSOLE_TRACE
    printf_s( buf );
#endif
    if (FileWriter::instance()->isOk())
    {
        fprintf_s(FileWriter::instance()->file(), buf);
        fflush(FileWriter::instance()->file());
    }
}

inline void DoTraceV( const wchar_t* fmt, va_list va_args )
{
    wchar_t buf[2048], userbuf[1024];
    vswprintf_s( userbuf, fmt, va_args );
    swprintf_s( buf, L"BlackBone: %ls\r\n", userbuf );
    OutputDebugStringW( buf );

#ifdef CONSOLE_TRACE
    wprintf_s( buf );
#endif
    if (FileWriter::instance()->isOk())
    {
        fwprintf_s(FileWriter::instance()->file(), buf);
        fflush(FileWriter::instance()->file());
    }
}

template<typename Ch>
inline void DoTrace( const Ch* fmt, ... )
{
    va_list va_args;
    va_start( va_args, fmt );
    DoTraceV( fmt, va_args );
    va_end( va_args );
}

#define BLACKBONE_TRACE(fmt, ...) DoTrace(fmt, ##__VA_ARGS__)

#else
#define BLACKBONE_TRACE(...)
#endif

}