// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-codegen.ll
// RUN: FileCheck --check-prefix=CIR --input-file=%t-cir.ll %s
// RUN: FileCheck --check-prefix=CODEGEN --input-file=%t-codegen.ll %s

// Regression test: vtable GlobalOp now correctly emits 'unnamed_addr'.
// V-tables have no identity significance, matching OGCG's getAddrOfVTable
// which calls VTable->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global).
//
// Note: CIR emits 'global' vs OGCG 'constant' — separate bug, not checked here.

class Base {
public:
  virtual void foo() {}
};

void test() {
  Base b;
  b.foo();
}

// Both should emit unnamed_addr attribute
// CIR: @_ZTV4Base = linkonce_odr unnamed_addr
// CODEGEN: @_ZTV4Base = linkonce_odr unnamed_addr
