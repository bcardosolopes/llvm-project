// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-call-conv-lowering -emit-mlir=cir-flat -mmlir --mlir-print-ir-after=cir-call-conv-lowering %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-call-conv-lowering -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// __int128 (i128) is passed directly on x86-64, not decomposed into {i64, i64}.

// CHECK: @_Z5test1nn(%arg0: !s128i {cir.noundef}{{.*}}, %arg1: !s128i {cir.noundef}{{.*}}) -> (!s128i {cir.noundef})
// LLVM: define dso_local noundef i128 @_Z5test1nn(i128 noundef %0, i128 noundef %1)
// OGCG: define dso_local noundef i128 @_Z5test1nn(i128 noundef %a, i128 noundef %b)
__int128 test1(__int128 a, __int128 b) {
  // CHECK: cir.store %arg0, %{{.+}} : !s128i, !cir.ptr<!s128i>
  // CHECK: cir.store %arg1, %{{.+}} : !s128i, !cir.ptr<!s128i>

  // LLVM: store i128 %0, ptr %{{.+}}, align 16
  // LLVM: store i128 %1, ptr %{{.+}}, align 16

  return a + b;
  // CHECK: %[[#A:]] = cir.load{{.*}} : !cir.ptr<!s128i>, !s128i
  // CHECK: %[[#B:]] = cir.load{{.*}} : !cir.ptr<!s128i>, !s128i
  // CHECK: %[[#SUM:]] = cir.binop(add, %[[#A]], %[[#B]]) nsw : !s128i

  // LLVM: %[[#A:]] = load i128, ptr %{{.+}}, align 16
  // LLVM: %[[#B:]] = load i128, ptr %{{.+}}, align 16
  // LLVM: %[[#SUM:]] = add nsw i128 %[[#A]], %[[#B]]

  // CHECK: cir.return %{{.+}} : !s128i
  // LLVM: ret i128 %{{.+}}
  // OGCG: ret i128 %{{.+}}
}
