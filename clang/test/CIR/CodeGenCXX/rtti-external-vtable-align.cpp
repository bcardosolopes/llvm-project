// RUN: %clang_cc1 -fclangir -triple x86_64-linux-unknown -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-linux-unknown -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// External RTTI vtable globals should not have explicit alignment.
// OGCG omits alignment on external typeinfo vtable declarations.

#pragma GCC visibility push(hidden)

struct Base {
  virtual ~Base() = default;
  virtual void* Alloc() = 0;
};

class Child : public Base {
public:
  Child() = default;
  void* Alloc();
};

void test() { Child x; }

// External RTTI vtable declaration should NOT have ", align 8" suffix.
// CIR: @_ZTVN10__cxxabiv117__class_type_infoE = external global [0 x ptr]{{$}}
// OGCG: @_ZTVN10__cxxabiv117__class_type_infoE = external global [0 x ptr]{{$}}
