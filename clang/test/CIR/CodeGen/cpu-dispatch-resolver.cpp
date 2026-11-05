// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that cpu_dispatch resolvers use an if-else-trap decision tree
// (matching OGCG), not a flat select chain.

struct S {
  __attribute__((cpu_specific(atom)))
  void Func(){}
  __attribute__((cpu_dispatch(ivybridge, atom)))
  void Func(){}
};

void foo() {
  S s;
  s.Func();
}

// CIR-LABEL: @_ZN1S4FuncEv.resolver()
// CIR:         cir.call @__cpu_indicator_init
//   Check ivybridge first (higher priority).
// CIR:         cir.brcond %{{.*}} ^bb1, ^bb2
// CIR:       ^bb1:
// CIR:         cir.return
// CIR:       ^bb2:
//   Then check atom.
// CIR:         cir.brcond %{{.*}} ^bb3, ^bb4
// CIR:       ^bb3:
// CIR:         cir.return
// CIR:       ^bb4:
//   No match → trap.
// CIR:         cir.trap

// LLVM-LABEL: @_ZN1S4FuncEv.resolver()
// LLVM:         call void @__cpu_indicator_init
//   ivybridge check.
// LLVM:         br i1 %{{.*}}, label %[[RET1:.*]], label %[[ELSE1:.*]]
// LLVM:       [[RET1]]:
// LLVM:         ret ptr @_ZN1S4FuncEv.S
// LLVM:       [[ELSE1]]:
//   atom check.
// LLVM:         br i1 %{{.*}}, label %[[RET2:.*]], label %[[ELSE2:.*]]
// LLVM:       [[RET2]]:
// LLVM:         ret ptr @_ZN1S4FuncEv.O
// LLVM:       [[ELSE2]]:
// LLVM:         call void @llvm.trap()
// LLVM-NEXT:    unreachable

// OGCG-LABEL: @_ZN1S4FuncEv.resolver()
// OGCG:         call void @__cpu_indicator_init
// OGCG:         br i1 %{{.*}}, label %[[RET1:.*]], label %[[ELSE1:.*]]
// OGCG:       [[RET1]]:
// OGCG:         ret ptr @_ZN1S4FuncEv.S
// OGCG:       [[ELSE1]]:
// OGCG:         br i1 %{{.*}}, label %[[RET2:.*]], label %[[ELSE2:.*]]
// OGCG:       [[RET2]]:
// OGCG:         ret ptr @_ZN1S4FuncEv.O
// OGCG:       [[ELSE2]]:
// OGCG:         call void @llvm.trap()
// OGCG-NEXT:    unreachable
