// UNSUPPORTED: system-aix
// UNSUPPORTED: target={{.*}}-zos{{.*}}
//
// RUN: rm -rf %t
// RUN: mkdir %t
// RUN: split-file %s %t
//
// Partition unit: check CIR emits module partition initializer function.
// RUN: %clang_cc1 -std=c++20 %t/Foo-impl.cppm -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o %t/Foo-impl.cir.ll
// RUN: FileCheck --input-file=%t/Foo-impl.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 %t/Foo-impl.cppm -triple x86_64-unknown-linux-gnu -emit-llvm -o %t/Foo-impl.ogcg.ll
// RUN: FileCheck --input-file=%t/Foo-impl.ogcg.ll %s --check-prefix=OGCG

// C++20 module partition must emit _ZGIW<module>WP<partition>
// initializer function and @llvm.global_ctors entry, even when partition
// has no dynamic initializers.

//--- Foo-impl.cppm
export module Foo:impl;

export void Quux() {}

// CIR: @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @_ZGIW3FooWP4impl, ptr null }]
// CIR: define void @_ZGIW3FooWP4impl() {{.*}} section ".text.startup"
// CIR:   ret void
//
// OGCG: @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @_ZGIW3FooWP4impl, ptr null }]
// OGCG: define void @_ZGIW3FooWP4impl() {{.*}} section ".text.startup"
// OGCG:   ret void
