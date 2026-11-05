// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// Union arguments on indirect calls should NOT get noundef.
// Union values may have undefined padding bits.

union U { int i; float f; };
typedef int (*fp)(U);

int test(fp func, U u) { return func(u); }

// The union arg should NOT have noundef. The return value should have noundef.
// CIR-LABEL: @_Z4testPFi1UES_
// CIR: call noundef i32 %{{[0-9]+}}(i32 %{{[0-9]+}})
// CIR-NOT: call noundef i32 %{{[0-9]+}}(i32 noundef
// OGCG-LABEL: @_Z4testPFi1UES_
// OGCG: call noundef i32 %{{[0-9]+}}(i32 %{{[0-9]+}})
