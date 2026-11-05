// RUN: %clang_cc1 -std=c++11 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// RUN: %clang_cc1 -std=c++11 -triple x86_64-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Verify CIR emits target_clones multiversion function bodies with correct
// mangled names matching OGCG.

int __attribute__((target_clones("sse4.2", "default"))) foo(int x) { return x + 1; }
int __attribute__((target_clones("arch=ivybridge", "default"))) bar(int x) { return x + 2; }

// CHECK: @_Z3fooi.ifunc = weak_odr alias i32 (i32), ptr @_Z3fooi
// CHECK: @_Z3bari.ifunc = weak_odr alias i32 (i32), ptr @_Z3bari
// CHECK: define {{.*}}i32 @_Z3fooi.sse4.2.0(i32 {{.*}}%{{.*}})
// CHECK: define {{.*}}i32 @_Z3fooi.default.1(i32 {{.*}}%{{.*}})
// CHECK: define {{.*}}i32 @_Z3bari.arch_ivybridge.0(i32 {{.*}}%{{.*}})
// CHECK: define {{.*}}i32 @_Z3bari.default.1(i32 {{.*}}%{{.*}})

// OGCG: @_Z3fooi.ifunc = weak_odr alias i32 (i32), ptr @_Z3fooi
// OGCG: @_Z3bari.ifunc = weak_odr alias i32 (i32), ptr @_Z3bari
// OGCG: define {{.*}}i32 @_Z3fooi.sse4.2.0(i32 {{.*}}%{{.*}})
// OGCG: define {{.*}}i32 @_Z3fooi.default.1(i32 {{.*}}%{{.*}})
// OGCG: define {{.*}}i32 @_Z3bari.arch_ivybridge.0(i32 {{.*}}%{{.*}})
// OGCG: define {{.*}}i32 @_Z3bari.default.1(i32 {{.*}}%{{.*}})
