// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O0 \
// RUN:   -o - %s | FileCheck %s

// Verify CIR emits ifunc/resolver dispatch for x86 target_clones functions.

// CHECK-DAG: @__cpu_model = external dso_local global
// CHECK-DAG: @foo = weak_odr ifunc i32 (i32), ptr @foo.resolver

__attribute__((target_clones("default", "sse4.2")))
int foo(int x) { return x + 1; }

// Versioned bodies.
// CHECK: define dso_local i32 @foo.default.1(i32 noundef %{{.*}}) #[[ATTR0:[0-9]+]]
// CHECK: define dso_local i32 @foo.sse4.2.0(i32 noundef %{{.*}}) #[[ATTR1:[0-9]+]]

// Resolver: calls __cpu_indicator_init and checks CPU features.
// CHECK: define weak_odr ptr @foo.resolver()
// CHECK:   call void @__cpu_indicator_init()
// CHECK:   load i32, ptr getelementptr inbounds
// CHECK:   and i32 %{{.*}}, 256
// CHECK:   icmp eq i32 %{{.*}}, 256
// CHECK:   ret ptr

// Caller goes through the ifunc, not a concrete clone.
int bar(int x) { return foo(x); }
// CHECK: define dso_local i32 @bar(
// CHECK:   call i32 @foo(

// Per-clone target features: sse4.2 clone gets sse4.2 features.
// CHECK: attributes #[[ATTR0]] = {{.*}}"target-features"="+cx8,+mmx,+sse,+sse2,+x87"
// CHECK: attributes #[[ATTR1]] = {{.*}}"target-features"="{{[^"]*}}+sse4.2{{[^"]*}}"
