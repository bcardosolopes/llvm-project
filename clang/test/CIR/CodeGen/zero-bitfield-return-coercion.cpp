// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s
// CIR loads the coerced return value from offset 0 instead of the
// correct offset 8 when a struct has a leading empty sub-struct from a
// zero-width bitfield. Fixed by adding inbounds to PtrStrideOps.

struct s8_0 { unsigned : 0; };
struct s8_1 { double x; };
struct s8 { s8_0 a; s8_1 b; };

// The return must load from offset 8 (where the double lives), not offset 0.
// CHECK-LABEL: @_Z2f8v
// CHECK: getelementptr inbounds i8, ptr %{{.*}}, i64 8
// CHECK: load double

// OGCG-LABEL: @_Z2f8v
// OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 8
// OGCG: load double
s8 f8() { return s8(); }
