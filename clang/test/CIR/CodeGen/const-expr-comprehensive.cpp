// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Comprehensive test for constant expression emission with OGCG parity.

extern const int vals[];
const int vals[] = {10, 20, 30, 40, 50};
// CHECK-DAG: @vals = {{.*}}constant [5 x i32] [i32 10, i32 20, i32 30, i32 40, i32 50]
// OGCG-DAG: @vals = {{.*}}constant [5 x i32] [i32 10, i32 20, i32 30, i32 40, i32 50]

struct BF { unsigned a : 4; unsigned b : 4; unsigned c : 8; unsigned d : 16; };
constexpr BF bf = {0xA, 0xB, 0xCD, 0xEF01};
// CHECK-DAG: @_ZL2bf = {{.*}}constant
// OGCG-DAG: @_ZL2bf = {{.*}}constant
int get_bf() { return bf.a + bf.d; }

union U { int i; float f; };
constexpr U u_int = {.i = 42};
// CHECK-DAG: @_ZL5u_int = {{.*}}constant {{.*}}i32 42
// OGCG-DAG: @_ZL5u_int = {{.*}}constant {{.*}}i32 42
int get_union() { return u_int.i; }

extern const int fwd[];
struct Ref { const int* p; int n; };
const Ref refs[] = {{fwd, 3}};
const int fwd[] = {100, 200, 300};
// CHECK-DAG: @fwd = {{.*}}constant [3 x i32] [i32 100, i32 200, i32 300]
// OGCG-DAG: @fwd = {{.*}}constant [3 x i32] [i32 100, i32 200, i32 300]
int get_fwd() { return *refs[0].p; }
