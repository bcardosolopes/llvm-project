// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// ABI coercion offset was wrong for struct return with zero-bitfield
// padding. CIR loaded from offset 0 instead of applying the coercion offset.

struct s8_0 { unsigned : 0; };
struct s8_1 { double x; };
struct s8 { s8_0 a; s8_1 b; };

// The struct s8 layout is { [8 x i8], %struct.s8_1 } where s8_1 contains a
// double at offset 8. The ABI coerces the return to double, requiring a GEP
// to byte offset 8 before loading.

// CHECK: define dso_local double @_Z2f8v()
// CHECK:   %[[ALLOCA:.*]] = alloca %struct.s8
// CHECK:   %[[GEP:.*]] = getelementptr inbounds i8, ptr %[[ALLOCA]], i64 8
// CHECK:   %[[LOAD:.*]] = load double, ptr %[[GEP]]
// CHECK:   ret double %[[LOAD]]

// OGCG: define dso_local double @_Z2f8v()
// OGCG:   %[[ALLOCA:.*]] = alloca %struct.s8
// OGCG:   %[[GEP:.*]] = getelementptr inbounds i8, ptr %[[ALLOCA]], i64 8
// OGCG:   %[[LOAD:.*]] = load double, ptr %[[GEP]]
// OGCG:   ret double %[[LOAD]]
s8 f8() { return s8(); }
