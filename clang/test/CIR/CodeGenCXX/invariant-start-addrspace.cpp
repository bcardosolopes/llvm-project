// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O1 \
// RUN:   -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -O1 \
// RUN:   -disable-llvm-passes %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// llvm.invariant.start is overloaded only on its operand pointer; its result is
// a token in the default address space regardless of the operand's address
// space. A constant global in a non-default address space with a runtime
// initializer must therefore emit llvm.invariant.start.pN with a default-
// address-space result. Previously the result type used the operand's address
// space, selecting a non-existent overload and failing intrinsic translation.

int f();

// A default-address-space const global uses the .p0 overload.
extern const int d = f();
// CIR: call ptr @llvm.invariant.start.p0(i64 4, ptr @d)
// OGCG: call ptr @llvm.invariant.start.p0(i64 4, ptr @d)

// A const global in address space 10 uses the .p10 overload, with a default-
// address-space (plain ptr) result.
extern const int __attribute__((address_space(10))) d2 = f();
// CIR: call ptr @llvm.invariant.start.p10(i64 4, ptr addrspace(10) @d2)
// OGCG: call ptr @llvm.invariant.start.p10(i64 4, ptr addrspace(10) @d2)
