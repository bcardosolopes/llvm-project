// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Simulate glibc's always_inline C++ wrappers (e.g. strchr) that call
// themselves through __builtin_ aliases. These are available_externally but
// trivially recursive, so we must NOT emit their bodies (PR9614).
//
// glibc provides C++ overloads of strchr that take char* (non-const) and
// return char* (non-const), wrapping __builtin_strchr. Because the overload
// has a different signature from the C builtin, it is NOT an inline builtin
// declaration; it gets available_externally linkage. Without the
// isTriviallyRecursive guard, its body is emitted and calls itself
// infinitely, causing LLVM to optimize it to unreachable.

extern "C" char *strchr(const char *, int) __attribute__((nothrow));

// C++ overload that glibc provides via <cstring>
extern __inline__ __attribute__((always_inline, gnu_inline))
char *strchr(char *__s, int __c) noexcept {
    return __builtin_strchr(__s, __c);
}

// The available_externally body should NOT be emitted because it is
// trivially recursive (strchr's body calls __builtin_strchr which resolves
// to strchr itself).
// CHECK-NOT: cir.func {{.*}}available_externally{{.*}} @strchr

char *test(char *s) {
    return strchr(s, 'A');
}
