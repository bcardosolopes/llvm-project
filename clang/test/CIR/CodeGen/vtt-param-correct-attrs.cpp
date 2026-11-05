// RUN: %clang_cc1 -fclangir -emit-llvm -std=c++20 -triple x86_64-unknown-linux-gnu %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// Test that VTT parameter in constructors with virtual bases gets
// only 'noundef', not the next parameter's nonnull/align/dereferenceable.

struct A { virtual void f(); };
struct B : virtual A { int x; B(); B(const B&); };
B::B() : x(42) {}
B::B(const B& o) : x(o.x) {}

// The copy constructor B(const B&) has params: this, vtt, const B&
// VTT should be just 'ptr noundef', not 'ptr noundef nonnull align 8 deref(12)'
// CHECK: define {{.*}} @_ZN1BC2ERKS_(ptr noundef nonnull align 8 dereferenceable(12) %{{.*}}, ptr noundef %{{.*}}, ptr noundef nonnull align 8 dereferenceable(12) %{{.*}})
