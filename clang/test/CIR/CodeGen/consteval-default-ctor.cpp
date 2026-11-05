// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=OGCG %s

// Consteval default constructor with inheritance — the APValue produces
// a flattened struct type while the alloca uses the nested CIR record type.

struct Base {
  void* one = nullptr;
  void* two = nullptr;
};

struct Derived : Base {
  void* three = nullptr;
  consteval Derived() = default;
};

void method() {
  (void)Derived();
}

// LLVM-LABEL: @_Z6methodv
// LLVM: alloca %struct.Derived
// LLVM: store { ptr, ptr, ptr } zeroinitializer, ptr

// OGCG-LABEL: @_Z6methodv
// OGCG: alloca %struct.Derived
