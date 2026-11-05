// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fms-extensions -o %t.cir %s
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fms-extensions -o %t.ll %s
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -fms-extensions -o %t.ogcg.ll %s
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=OGCG

// Verify that __builtin_trap, __debugbreak, and __builtin_verbose_trap are
// correctly lowered and that [[clang::nomerge]] is propagated to trap calls.

class A {
public:
  [[clang::nomerge]] ~A();
};

void test_trap_no_nomerge() {
  __builtin_trap();
}

// CIR-LABEL: @_Z20test_trap_no_nomergev
// CIR: cir.call_llvm_intrinsic "trap"
// CIR-NOT: cir.nomerge

// LLVM-LABEL: @_Z20test_trap_no_nomergev
// LLVM: call void @llvm.trap(){{$}}

// OGCG-LABEL: @_Z20test_trap_no_nomergev
// OGCG: call void @llvm.trap(){{$}}

void test_trap_nomerge() {
  A a;
  [[clang::nomerge]] __builtin_trap();
  [[clang::nomerge]] __debugbreak();
  [[clang::nomerge]] __builtin_verbose_trap("category", "message");
}

// CIR-LABEL: @_Z17test_trap_nomergev
// CIR: cir.call_llvm_intrinsic "trap" {{.*}} {cir.nomerge}
// CIR: cir.call_llvm_intrinsic "debugtrap" {{.*}} {cir.nomerge}
// CIR: cir.call_llvm_intrinsic "trap" {{.*}} {cir.nomerge}

// LLVM-LABEL: @_Z17test_trap_nomergev
// LLVM: call void @llvm.trap() #[[NOMERGE:[0-9]+]]
// LLVM: call void @llvm.debugtrap() #[[NOMERGE]]
// LLVM: call void @llvm.trap() #[[NOMERGE]]
// LLVM: call void @_ZN1AD1Ev({{.*}}) #[[NOMERGE_NUW:[0-9]+]]
// LLVM-DAG: attributes #[[NOMERGE]] = { nomerge }
// LLVM-DAG: attributes #[[NOMERGE_NUW]] = {{{.*}}nomerge{{.*}}}

// OGCG-LABEL: @_Z17test_trap_nomergev
// OGCG: call void @llvm.trap() #[[NOMERGE:[0-9]+]]
// OGCG: call void @llvm.debugtrap() #[[NOMERGE]]
// OGCG: call void @llvm.trap() #[[NOMERGE]]
// OGCG: call void @_ZN1AD1Ev({{.*}}) #[[NOMERGE_NUW:[0-9]+]]
// OGCG-DAG: attributes #[[NOMERGE]] = { nomerge }
// OGCG-DAG: attributes #[[NOMERGE_NUW]] = {{{.*}}nomerge{{.*}}}
