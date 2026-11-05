// RUN: %clang_cc1 -x objective-c++ -fclangir -fsanitize=nullability-return \
// RUN:   -emit-llvm %s -o - -triple x86_64-apple-macosx10.10.0 \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -x objective-c++ -fclangir -fsanitize=nullability-return \
// RUN:   -emit-mlir=cir-abi %s -o - -triple x86_64-apple-macosx10.10.0 \
// RUN:   | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -x objective-c++ -fclangir -fsanitize=nullability-return \
// RUN:   -emit-mlir=cir-abi %s -o %t.cir -triple x86_64-apple-macosx10.10.0
// RUN: cir-translate -cir-to-llvmir %t.cir | FileCheck %s --check-prefix=LLVM

// LLVM: @.ret.nonnull.sloc = {{.*}} i32 {{[0-9]+}}, i32 3
// LLVM: [[ATTR_LOC:@[0-9]+]] = {{.*}} global { {{.*}} ptr @.src, i32 {{[0-9]+}}, i32 38 }
//
// LLVM-LABEL: define{{.*}} ptr @_Z3foov()
// LLVM: call noundef ptr @_Z6helperv()
// LLVM: load ptr, ptr {{.*}}align 8{{.*}}!nosanitize
// LLVM: icmp ne ptr {{.*}}, null{{.*}}!nosanitize
// LLVM: and i1 {{.*}}, true{{.*}}!nosanitize
// LLVM: call void @__ubsan_handle_nullability_return_v1_abort(ptr [[ATTR_LOC]], i64 ptrtoint (ptr [[ATTR_LOC]] to i64))

// CIR: cir.global "private"  cir_private dso_local  @".ret.nonnull.sloc" = {{.*}}#cir.int<3> : !s32i
// CIR-LABEL: cir.func no_inline optnone @_Z3foov()
// CIR: %[[PRE:.*]] = cir.const #true
// CIR: cir.store %{{.*}}, %{{.*}} {cir.nosanitize} : !cir.ptr<!u8i>, !cir.ptr<!cir.ptr<!u8i>>
// CIR: %[[CALL:.*]] = cir.call @_Z6helperv() : () -> !cir.ptr<!void>
// CIR: %[[SLOC:.*]] = cir.load align(8) %{{.*}} : !cir.ptr<!cir.ptr<!u8i>>, !cir.ptr<!u8i> {cir.nosanitize}
// CIR: %[[CAN:.*]] = cir.cmp(ne, %[[SLOC]], %{{.*}}) : !cir.ptr<!u8i>, !cir.bool {cir.nosanitize}
// CIR: %[[GATE:.*]] = cir.binop(and, %[[CAN]], %[[PRE]]) : !cir.bool {cir.nosanitize}
// CIR: cir.call @__cir_ubsan_nonnull_return(%{{.*}}) {cir.call_nomerge, cir.nosanitize, cir.ubsan.attr.source_column = 38 : i32, cir.ubsan.attr.source_file = "{{.*}}ubsan-nullability-return-notypeloc.mm", cir.ubsan.attr.source_line = {{[0-9]+}} : i32, cir.ubsan.handler_kind = "abort", cir.ubsan.handler_name = "nullability_return"}

struct S {
  using PtrTy = id;
};

#pragma clang assume_nonnull begin
__attribute__((ns_returns_retained)) S::PtrTy foo(void) {
  extern S::PtrTy helper(void);
  return helper();
}
#pragma clang assume_nonnull end
