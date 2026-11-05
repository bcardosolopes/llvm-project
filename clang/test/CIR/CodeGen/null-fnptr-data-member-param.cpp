// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Null function pointer whose signature includes a data_member
// parameter type must be lowered correctly through CXXABILowering.

struct Base { int val; };
struct Derived : Base {
  typedef int Base::*MemPtr;
  void* (*applyMemberPtr_)(void*, MemPtr) = nullptr;
  MemPtr memberPtr_ = nullptr;
};
Derived make() { return Derived(); }

// CIR: cir.func {{.*}} @_Z4makev
// CIR:   cir.const #cir.const_record<{
// CIR-SAME: #cir.ptr<null>
// CIR-SAME: #cir.int<-1>
// CIR-SAME: }>

// LLVM: define {{.*}} @_Z4makev
// LLVM:   store %struct.Derived { %struct.Base zeroinitializer, ptr null, i64 -1 }
