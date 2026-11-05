// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

void test_assign(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  int (*p)[n] = __builtin_va_arg(ap, int (*)[n]);
  (void)p;
  __builtin_va_end(ap);
}

// Verify the va_arg type itself still evaluates variably modified bounds even
// when there is no local declaration of the same VLA type to do that work.
int test_sidefx(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  (void)__builtin_va_arg(ap, int (*)[++n]);
  __builtin_va_end(ap);
  return n;
}

// CIR: !rec___va_list_tag = !cir.record<struct "__va_list_tag" {!u32i, !u32i, !cir.ptr<!void>, !cir.ptr<!void>}
//
// CIR-LABEL: cir.func {{.*}} @test_assign(
// CIR: cir.va_start
// CIR: %[[VAARG:.+]] = cir.scope {
// CIR: cir.yield %{{.*}} : !cir.ptr<!s32i>
// CIR: } : !cir.ptr<!s32i>
// CIR: cir.store align(8) %[[VAARG]], {{.*}} : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
// CIR: cir.va_end
//
// CIR-LABEL: cir.func {{.*}} @test_sidefx(
// CIR: %[[NADDR:.+]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["n", init]
// CIR: cir.va_start
// CIR: %[[NVAL:.+]] = cir.load align(4) %[[NADDR]] : !cir.ptr<!s32i>, !s32i
// CIR-NEXT: %[[INC:.+]] = cir.unary(inc, %[[NVAL]]) nsw : !s32i, !s32i
// CIR-NEXT: cir.store align(4) %[[INC]], %[[NADDR]] : !s32i, !cir.ptr<!s32i>
// CIR: cir.scope {
// CIR: cir.yield %{{.*}} : !cir.ptr<!s32i>
// CIR: } : !cir.ptr<!s32i>
// CIR: cir.va_end
// CIR: %[[RETN:.+]] = cir.load align(4) %[[NADDR]] : !cir.ptr<!s32i>, !s32i
// CIR: cir.return %{{.*}} : !s32i
//
// LLVM-LABEL: define dso_local void @test_assign(
// LLVM: call void @llvm.va_start.p0(ptr %{{.*}})
// LLVM: %{{.*}} = phi ptr [ %{{.*}}, %{{.*}} ], [ %{{.*}}, %{{.*}} ]
// LLVM-NEXT: %{{.*}} = load ptr, ptr %{{.*}}, align 8
// LLVM: call void @llvm.va_end.p0(ptr %{{.*}})
//
// LLVM-LABEL: define dso_local i32 @test_sidefx(
// LLVM: %[[NADDR:.+]] = alloca i32
// LLVM: call void @llvm.va_start.p0(ptr %{{.*}})
// LLVM: %[[N0:.+]] = load i32, ptr %[[NADDR]], align 4
// LLVM-NEXT: %[[INC:.+]] = add nsw i32 %[[N0]], 1
// LLVM-NEXT: store i32 %[[INC]], ptr %[[NADDR]], align 4
// LLVM: %{{.*}} = phi ptr [ %{{.*}}, %{{.*}} ], [ %{{.*}}, %{{.*}} ]
// LLVM: call void @llvm.va_end.p0(ptr %{{.*}})
// LLVM: %[[RETN:.+]] = load i32, ptr %[[NADDR]], align 4
// LLVM: ret i32 %{{.*}}
//
// OGCG-LABEL: define dso_local i32 @test_sidefx(
// OGCG: %[[NADDR:.+]] = alloca i32
// OGCG: call void @llvm.va_start.p0(ptr %{{.*}})
// OGCG: %[[N0:.+]] = load i32, ptr %[[NADDR]], align 4
// OGCG-NEXT: %[[INC:.+]] = add nsw i32 %[[N0]], 1
// OGCG-NEXT: store i32 %[[INC]], ptr %[[NADDR]], align 4
// OGCG: %[[VAARG_ADDR:.+]] = phi ptr [ %{{.*}}, %vaarg.in_reg ], [ %{{.*}}, %vaarg.in_mem ]
// OGCG-NEXT: %{{.*}} = load ptr, ptr %[[VAARG_ADDR]], align 8
// OGCG: call void @llvm.va_end.p0(ptr %{{.*}})
// OGCG: %[[RETN:.+]] = load i32, ptr %[[NADDR]], align 4
// OGCG-NEXT: ret i32 %[[RETN]]
