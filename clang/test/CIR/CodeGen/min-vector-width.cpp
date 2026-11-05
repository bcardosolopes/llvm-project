// RUN: %clang_cc1 -std=c++17 -fcxx-exceptions -fexceptions -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -fcxx-exceptions -fexceptions -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -fcxx-exceptions -fexceptions -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test that min-legal-vector-width is set correctly based on vector
// argument/return types, matching OGCG behavior.

typedef int v4si __attribute__((vector_size(16)));
typedef double v4df __attribute__((vector_size(32)));

// 128-bit vector args → width 128
// CHECK: define {{.*}} @_Z7vec_addDv4_iS_({{.*}}) #[[W128:[0-9]+]]
// OGCG: define {{.*}} @_Z7vec_addDv4_iS_({{.*}}) #[[W128:[0-9]+]]
v4si vec_add(v4si a, v4si b) { return a + b; }

// 256-bit vector args → width 256
// CHECK: define {{.*}} @_Z8vec_add2Dv4_dS_({{.*}}) #[[W256:[0-9]+]]
// OGCG: define {{.*}} @_Z8vec_add2Dv4_dS_({{.*}}) #[[W256:[0-9]+]]
v4df vec_add2(v4df a, v4df b) { return a + b; }

// No vector → width 0
// CHECK: define {{.*}} @_Z5plaini({{.*}}) #[[W0:[0-9]+]]
// OGCG: define {{.*}} @_Z5plaini({{.*}}) #[[W0:[0-9]+]]
int plain(int x) { return x + 1; }

// Vector types that are ABI-coerced to scalar should not force non-zero
// min-legal-vector-width in OGCG/CIR.
typedef char c8 __attribute__((vector_size(8)));
typedef long long ll1 __attribute__((vector_size(8)));
// CHECK: define {{.*}} @_Z10coerce_argDv8_c(double{{.*}}) #[[W0]]
// OGCG: define {{.*}} @_Z10coerce_argDv8_c(double{{.*}}) #[[W0]]
void coerce_arg(c8 v) {}
// CHECK: define {{.*}} @_Z10coerce_retv() #[[W0]]
// OGCG: define {{.*}} @_Z10coerce_retv() #[[W0]]
c8 coerce_ret() { return (c8){0}; }
// CHECK: define {{.*}} @_Z11coerce_arg2Dv1_x(double{{.*}}) #[[W0]]
// OGCG: define {{.*}} @_Z11coerce_arg2Dv1_x(double{{.*}}) #[[W0]]
void coerce_arg2(ll1 v) {}

// _Complex float is ABI-lowered to <2 x float> (64-bit vector) on x86_64.
// CHECK: define {{.*}} @_Z11complex_addCfS_({{.*}}) #[[W64:[0-9]+]]
// OGCG: define {{.*}} @_Z11complex_addCfS_({{.*}}) #[[W64:[0-9]+]]
_Complex float complex_add(_Complex float a, _Complex float b) {
    return a + b;
}

// Small struct coerced to <2 x float> by ABI.
struct Vec2 { float x, y; };
// CHECK: define {{.*}} @_Z8vec2_add4Vec2S_(<2 x float>{{.*}}, <2 x float>{{.*}}) #[[W64]]
// OGCG: define {{.*}} @_Z8vec2_add4Vec2S_(<2 x float>{{.*}}, <2 x float>{{.*}}) #[[W64]]
Vec2 vec2_add(Vec2 a, Vec2 b) {
    return {a.x + b.x, a.y + b.y};
}

// Call-site vector propagation: a function that doesn't use vectors directly
// but calls a function whose return type is ABI-lowered to a vector should
// inherit the vector width.
_Complex float ret_cf(void);
void use(void*);
// CHECK: define {{.*}} @_Z7call_cfv({{.*}}) #[[W64CALL:[0-9]+]]
// OGCG: define {{.*}} @_Z7call_cfv({{.*}}) #[[W64CALL:[0-9]+]]
void call_cf() { _Complex float c = ret_cf(); use(&c); }

struct Vec2 ret_v2(void);
// CHECK: define {{.*}} @_Z7call_v2v({{.*}}) #[[W64CALL]]
// OGCG: define {{.*}} @_Z7call_v2v({{.*}}) #[[W64CALL]]
void call_v2() { Vec2 v = ret_v2(); use(&v); }

void take_v2(Vec2);
// CHECK: define {{.*}} @_Z11call_arg_v2v({{.*}}) #[[W64CALL]]
// OGCG: define {{.*}} @_Z11call_arg_v2v({{.*}}) #[[W64CALL]]
void call_arg_v2() {
    Vec2 v = {1.0f, 2.0f};
    take_v2(v);
}

Vec2 may_throw_v2(void);
// CHECK: define {{.*}} @_Z11call_try_v2v({{.*}}) #[[W64CALL]]
// OGCG: define {{.*}} @_Z11call_try_v2v({{.*}}) #[[W64CALL]]
void call_try_v2() {
    try {
        Vec2 v = may_throw_v2();
        use(&v);
    } catch (...) {
    }
}

// A function that only calls plain functions should stay at width 0.
int ret_int(void);
// CHECK: define {{.*}} @_Z8call_intv({{.*}}) #[[W0CALL:[0-9]+]]
// OGCG: define {{.*}} @_Z8call_intv({{.*}}) #[[W0CALL:[0-9]+]]
void call_int() { int x = ret_int(); use(&x); }

// CHECK-DAG: attributes #[[W128]] = {{.*}}"min-legal-vector-width"="128"
// CHECK-DAG: attributes #[[W256]] = {{.*}}"min-legal-vector-width"="256"
// CHECK-DAG: attributes #[[W64]] = {{.*}}"min-legal-vector-width"="64"
// CHECK-DAG: attributes #[[W64CALL]] = {{.*}}"min-legal-vector-width"="64"
// CHECK-DAG: attributes #[[W0]] = {{.*}}"min-legal-vector-width"="0"
// CHECK-DAG: attributes #[[W0CALL]] = {{.*}}"min-legal-vector-width"="0"

// CIR: "min-legal-vector-width" = "0"

// OGCG-DAG: attributes #[[W128]] = {{.*}}"min-legal-vector-width"="128"
// OGCG-DAG: attributes #[[W256]] = {{.*}}"min-legal-vector-width"="256"
// OGCG-DAG: attributes #[[W64]] = {{.*}}"min-legal-vector-width"="64"
// OGCG-DAG: attributes #[[W64CALL]] = {{.*}}"min-legal-vector-width"="64"
// OGCG-DAG: attributes #[[W0]] = {{.*}}"min-legal-vector-width"="0"
// OGCG-DAG: attributes #[[W0CALL]] = {{.*}}"min-legal-vector-width"="0"
