// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll

// When a struct parameter is expanded into multiple ABI arguments,
// reference parameter attributes (nonnull, align, dereferenceable) were
// misapplied to the expanded struct fields due to stale arg_attrs from
// cloneWithoutRegions.

struct S { const char *p; unsigned long n; };

// LLVM-LABEL: define {{.*}} @_Z1f1SRi
// The second ABI arg (i64, from S.n) must NOT have nonnull/align/dereferenceable.
// The third ABI arg (ptr, from int &r) SHOULD have them.
// LLVM-SAME: ptr %{{[0-9]+}}, i64 %{{[0-9]+}}, ptr noundef nonnull align 4 dereferenceable(4) %{{[0-9]+}}
int f(S s, int &r) { return r; }

int test() { int r = 0; return f({"x", 1}, r); }

// The same remapping must happen for indirect calls. The expanded
// StringRef-like ABI pair from S must not steal the reference attrs from the
// trailing int& argument.
using IndirectFn = void (*)(S, int &);

// LLVM-LABEL: define {{.*}} @_Z13call_indirectPFv1SRiES0_
// LLVM: call void %{{[0-9]+}}(ptr %{{[0-9]+}}, i64 %{{[0-9]+}}, ptr noundef nonnull align 4 dereferenceable(4) %{{[0-9]+}})
void call_indirect(IndirectFn fp, int &r) { fp({"x", 1}, r); }
