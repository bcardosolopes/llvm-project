// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=OGCG %s

// Naked free function with inline asm.
__attribute__((naked)) void naked_free() {
  __asm__("ret");
}
// CIR-LABEL: cir.func {{.*}}@_Z10naked_freev
// CIR: cir.asm
// CIR: cir.unreachable
// LLVM-LABEL: @_Z10naked_freev
// LLVM: call void asm sideeffect "ret"
// LLVM: unreachable
// OGCG-LABEL: @_Z10naked_freev
// OGCG: call void asm sideeffect "ret"
// OGCG: unreachable

// Naked member function.
class TestNaked {
public:
  void NakedFunction();
};

__attribute__((naked)) void TestNaked::NakedFunction() {
  __asm__("");
}
// CIR-LABEL: cir.func {{.*}}@_ZN9TestNaked13NakedFunctionEv
// CIR: cir.asm
// LLVM-LABEL: @_ZN9TestNaked13NakedFunctionEv
// LLVM: call void asm sideeffect ""
// OGCG-LABEL: @_ZN9TestNaked13NakedFunctionEv
// OGCG: call void asm sideeffect ""

// Naked function with parameter — must NOT emit alloca/store for the param.
// This matches OGCG's attr-naked.c:t3.
__attribute__((naked)) void naked_with_param(int x) {
  __asm__("");
}
// LLVM-LABEL: @_Z16naked_with_parami
// LLVM-NOT: alloca
// LLVM-NOT: store
// LLVM: call void asm sideeffect ""
// OGCG-LABEL: @_Z16naked_with_parami
// OGCG-NOT: alloca
// OGCG-NOT: store
// OGCG: call void asm sideeffect ""

// Verify the naked attribute appears in CIR extra attributes and LLVM output.
// CIR: naked
// LLVM: attributes {{.*}}naked
// OGCG: attributes {{.*}}naked
