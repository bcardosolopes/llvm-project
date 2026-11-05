// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that reference parameters get nonnull, align, and dereferenceable
// attributes, matching OGCG behavior (CGCall.cpp ConstructAttributeList).

// CHECK: define {{.*}} @_Z1fRKi(ptr noundef nonnull align 4 dereferenceable(4) %0)
int f(const int& x) { return x; }

struct Big { int a, b, c, d; };

// CHECK: define {{.*}} @_Z1gRK3Big(ptr noundef nonnull align 4 dereferenceable(16) %0)
int g(const Big& b) { return b.a; }

// CHECK: define {{.*}} @_Z1hRi(ptr noundef nonnull align 4 dereferenceable(4) %0)
void h(int& x) { x = 42; }

// CHECK: define {{.*}} @_Z1kOi(ptr noundef nonnull align 4 dereferenceable(4) %0)
int k(int&& x) { return x; }

struct alignas(32) Aligned { int v; };

// CHECK: define {{.*}} @_Z1lRK7Aligned(ptr noundef nonnull align 32 dereferenceable(32) %0)
int l(const Aligned& a) { return a.v; }

// Verify attributes propagate to call sites as well.
// CHECK: define {{.*}} @_Z6callerv()
// CHECK:   call noundef i32 @_Z1fRKi(ptr noundef nonnull align 4 dereferenceable(4)
int caller() {
  int x = 10;
  return f(x);
}

// Incomplete types: no dereferenceable, but nonnull and align 1 (matching OGCG).
struct ForwardDecl;

template<typename T>
bool compare(const ForwardDecl& a, const ForwardDecl& b) {
  return true;
}
template bool compare<char>(const ForwardDecl&, const ForwardDecl&);
// CHECK: define {{.*}} @_Z7compareIcEbRK11ForwardDeclS2_(ptr noundef nonnull align 1 %0, ptr noundef nonnull align 1 %1)
