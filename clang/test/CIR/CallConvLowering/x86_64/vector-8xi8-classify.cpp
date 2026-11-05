// RUN: %clang_cc1 -triple x86_64-apple-darwin -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-apple-darwin -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Regression: x86-64 Darwin vector classification must not
// treat every 64-bit integer-element vector as INTEGER. Only one-lane 64-bit
// integer vectors (e.g. <1 x long long>) use the compatibility integer path.
// For <8 x i8>, OGCG classifies as SSE and coerces via double.

typedef char vec8char __attribute__((ext_vector_type(8)));
typedef long long vec1ll __attribute__((ext_vector_type(1)));

// CIR-LABEL: define{{.*}} @_Z10f_vec8charDv8_c(double noundef %{{.*}})
// OGCG-LABEL: define{{.*}} @_Z10f_vec8charDv8_c(double noundef %{{.*}})
void f_vec8char(vec8char v) {}

// CIR-LABEL: define{{.*}}double @_Z10r_vec8charv()
// OGCG-LABEL: define{{.*}}double @_Z10r_vec8charv()
vec8char r_vec8char() { return (vec8char){}; }

// One-lane 64-bit integer vector remains INTEGER-classified.
// CIR-LABEL: define{{.*}} @_Z8f_vec1llDv1_x(i64 noundef %{{.*}})
// OGCG-LABEL: define{{.*}} @_Z8f_vec1llDv1_x(i64 noundef %{{.*}})
void f_vec1ll(vec1ll v) {}

// CIR-LABEL: define{{.*}}i64 @_Z8r_vec1llv()
// OGCG-LABEL: define{{.*}}i64 @_Z8r_vec1llv()
vec1ll r_vec1ll() { return (vec1ll){}; }
