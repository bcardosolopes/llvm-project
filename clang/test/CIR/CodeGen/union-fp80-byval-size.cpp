// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -O0 -emit-llvm %s -o - | FileCheck %s

// Regression test: a by-value union whose highest-aligned member is x86_fp80
// (long double on x86-64) must be copied at its alloc size (16), not the
// x86_fp80 store size (10). Previously RecordType::computeUnionSize used the
// raw store size, so the by-value copy dropped the union tail (here the second
// double of the aliased struct), miscompiling f(u) to 1.5 instead of 4.0.

union LDU { long double ld; struct { double a, b; } s; };
double f(LDU u);
double g() { LDU u; u.s.a = 1.5; u.s.b = 2.5; return f(u); }

// CHECK-LABEL: @_Z1gv
// CHECK: call void @llvm.memcpy.{{.*}}(ptr align 16 {{.*}}, ptr align 16 {{.*}}, i64 16, i1 false)
