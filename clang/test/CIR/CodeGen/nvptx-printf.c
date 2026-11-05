// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fno-builtin-vprintf -fclangir \
// RUN:            -O2 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fno-builtin-vprintf \
// RUN:            -O2 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Device-side printf lowers to the two-argument CUDA device runtime function
// `int vprintf(char *fmt, void *buffer)`. Classic ref: clang/lib/CodeGen/
// CGGPUBuiltin.cpp GetVprintfDeclaration + EmitNVPTXDevicePrintfCallExpr.
//
// The regression this guards: glibc <stdio.h> supplies a __gnu_inline `vprintf`
// whose body is `vfprintf(stdout, fmt, ap)`. That definition is NOT recognized
// as an inline-builtin (so it is not renamed to `vprintf.inline`); here we
// reproduce that hermetically with -fno-builtin-vprintf. Previously the device
// printf path routed through the default deferred createRuntimeFunction path,
// which moved this definition into deferredDeclsToEmit and emitted an
// available_externally `@vprintf` body at -O>=1. The inliner then interposed
// it, rewriting the device call to `@vfprintf` -- a host stdio symbol that does
// not exist in the CUDA device runtime and survives all the way to PTX. The fix
// keeps `@vprintf` a pure external declaration (dontDefer), matching classic.

typedef __builtin_va_list va_list;
extern void *stdout;
extern int vfprintf(void *stream, const char *fmt, va_list ap);
extern int printf(const char *fmt, ...);

__attribute__((__always_inline__, __gnu_inline__)) extern inline int
vprintf(const char *fmt, va_list ap) {
  return vfprintf(stdout, fmt, ap);
}

void k(int x) { printf("%d\n", x); }

// The device call must stay a plain 2-arg `@vprintf`, and the host stdio
// `@vfprintf` inline body must not be interposed into the caller.

// LLVM-LABEL: define dso_local void @k(
// LLVM:         call i32 @vprintf(ptr {{.*}}, ptr {{.*}})
// LLVM-NOT:     vfprintf
// LLVM:         ret void
// LLVM-NOT:     vfprintf

// OGCG-LABEL: define dso_local void @k(
// OGCG:         call i32 @vprintf(ptr {{.*}}, ptr {{.*}})
// OGCG-NOT:     vfprintf
// OGCG:         ret void
// OGCG-NOT:     vfprintf
