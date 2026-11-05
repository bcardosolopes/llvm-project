// RUN: %clang_cc1 -std=c++17 -O1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -O0 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.o0.cir
// RUN: FileCheck --input-file=%t.o0.cir %s --check-prefix=CIR-O0
// RUN: %clang_cc1 -std=c++17 -O1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -O1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

void use(int *);

void foo() {
  int x = 42;
  use(&x);
}

// CIR: cir.func {{.*}} @_Z3foov()
// CIR:   %[[ADDR:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init]
// CIR:   cir.lifetime.start %[[ADDR]] : !cir.ptr<!s32i>
// CIR:   cir.lifetime.end %[[ADDR]] : !cir.ptr<!s32i>

// CIR-O0: cir.func {{.*}} @_Z3foov()
// CIR-O0-NOT: cir.lifetime

// LLVM: define{{.*}} void @_Z3foov()
// LLVM:   %[[ADDR:.*]] = alloca i32
// LLVM:   call void @llvm.lifetime.start
// LLVM:   call void @llvm.lifetime.end

// OGCG: define{{.*}} void @_Z3foov()
// OGCG:   %[[ADDR:.*]] = alloca i32
// OGCG:   call void @llvm.lifetime.start
// OGCG:   call void @llvm.lifetime.end
