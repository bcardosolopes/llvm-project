// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-codegen.ll
// RUN: FileCheck --check-prefix=CIR --input-file=%t-cir.ll %s
// RUN: FileCheck --check-prefix=CODEGEN --input-file=%t-codegen.ll %s

// This test verifies that CIR and CodeGen both emit the 'comdat' attribute on
// vtables. This is needed because:
// 1. Enables proper handling of linkonce_odr definitions across translation units
// 2. Ensures the linker can safely discard duplicate vtable definitions
// 3. Required for correct C++ semantics with inline/template classes
// 4. CodeGen has always emitted them with this attribute

class Base {
public:
  virtual void foo() {}
};

void test() {
  Base b;
  b.foo();
}

// Both should emit comdat attribute
// CIR: @_ZTV4Base = {{.*}}, comdat
// CODEGEN: @_ZTV4Base = {{.*}}, comdat
