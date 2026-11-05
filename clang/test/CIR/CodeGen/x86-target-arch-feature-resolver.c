// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Verify direct-CIR preserves OGCG's combined arch+feature resolver condition.

__attribute__((target("default")))
int mixed(int x) { return x; }

__attribute__((target("arch=sandybridge,sse4.2")))
int mixed(int x) { return x + 1; }

int call_mixed(int x) { return mixed(x); }

// LLVM: @__cpu_model = external dso_local global { i32, i32, i32, [1 x i32] }
// LLVM: @mixed.ifunc = weak_odr ifunc i32 (i32), ptr @mixed.resolver
// LLVM-LABEL: define weak_odr ptr @mixed.resolver()
// LLVM: call void @__cpu_indicator_init()
// LLVM: [[ARCHCMP:%.*]] = icmp eq i32 %{{.*}}, 3
// LLVM: [[FEATCMP:%.*]] = icmp eq i32 %{{.*}}, 256
// LLVM: [[COND:%.*]] = and i1 [[ARCHCMP]], %{{.*}}
// LLVM-NEXT: br i1 [[COND]], label %{{.*}}, label %{{.*}}
// LLVM: ret ptr @mixed.arch_sandybridge_sse4.2
// LLVM: ret ptr @mixed
// LLVM-LABEL: define dso_local i32 @call_mixed(
// LLVM: call i32 @mixed.ifunc(

// OGCG: @__cpu_model = external dso_local global { i32, i32, i32, [1 x i32] }
// OGCG: @mixed.ifunc = weak_odr ifunc i32 (i32), ptr @mixed.resolver
// OGCG-LABEL: define weak_odr ptr @mixed.resolver()
// OGCG: call void @__cpu_indicator_init()
// OGCG: [[ARCHCMP:%.*]] = icmp eq i32 %{{.*}}, 3
// OGCG: [[FEATCMP:%.*]] = icmp eq i32 %{{.*}}, 256
// OGCG: [[COND:%.*]] = and i1 [[ARCHCMP]], %{{.*}}
// OGCG-NEXT: br i1 [[COND]], label %{{.*}}, label %{{.*}}
