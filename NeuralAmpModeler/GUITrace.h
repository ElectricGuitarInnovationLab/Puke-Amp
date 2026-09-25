#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

// Opt-in diagnostics for native editor embedding. Launch the host with
// PUKE_AMP_GUI_TRACE=1; normal plugin runs do not create or write the log.
inline bool PukeAmpGUITraceEnabled()
{
  const char* value = std::getenv("PUKE_AMP_GUI_TRACE");
  return value && value[0] && std::strcmp(value, "0") != 0;
}

inline void PukeAmpGUITrace(const char* format, ...)
{
  if (!PukeAmpGUITraceEnabled())
    return;

  static std::mutex mutex;
  const std::lock_guard<std::mutex> lock(mutex);
  if (FILE* file = std::fopen("/tmp/PukeAmpGUITrace.log", "a"))
  {
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fclose(file);
  }
}
