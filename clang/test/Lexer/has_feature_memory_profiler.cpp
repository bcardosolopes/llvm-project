// RUN: %clang -E -fmemory-profile %s -o - | FileCheck --check-prefix=MEMPROF %s
// RUN: %clang -E -fmemory-profile=/tmp %s -o - | FileCheck --check-prefix=MEMPROF %s
// RUN: %clang -E %s -o - | FileCheck --check-prefix=NO-MEMPROF %s

#if __has_feature(memory_profiler)
int MemoryProfilerEnabled();
#else
int MemoryProfilerDisabled();
#endif

// MEMPROF: MemoryProfilerEnabled
// NO-MEMPROF: MemoryProfilerDisabled
