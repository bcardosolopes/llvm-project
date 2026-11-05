// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.cir
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// CIR was emitting packed struct type <{ ptr, ptr, ptr }> instead of
// array type [3 x ptr] for global arrays initialized with implicit pointer casts.

extern double FOO, BAR, XX;

// When void* array elements are initialized with double* addresses, the
// GlobalViewAttr type must match the destination (void*), not the source (double*).
// This ensures uniform element types so emitArrayConstant produces an array, not
// a packed struct.
void *Data[] = { &FOO, &BAR, &XX };

// CIR: cir.global external @Data = #cir.const_array<[
// CIR-SAME: #cir.global_view<@FOO> : !cir.ptr<!void>,
// CIR-SAME: #cir.global_view<@BAR> : !cir.ptr<!void>,
// CIR-SAME: #cir.global_view<@XX> : !cir.ptr<!void>
// CIR-SAME: ]> : !cir.array<!cir.ptr<!void> x 3>

// LLVM: @Data = global [3 x ptr] [ptr @FOO, ptr @BAR, ptr @XX], align 16

// OGCG: @Data = global [3 x ptr] [ptr @FOO, ptr @BAR, ptr @XX], align 16

// Mixed types: float* and int* stored in void*[] should also produce array type.
extern float F1;
extern int I1;
void *Mixed[] = { &F1, &I1 };

// CIR: cir.global external @Mixed = #cir.const_array<[
// CIR-SAME: #cir.global_view<@F1> : !cir.ptr<!void>,
// CIR-SAME: #cir.global_view<@I1> : !cir.ptr<!void>
// CIR-SAME: ]> : !cir.array<!cir.ptr<!void> x 2>

// LLVM: @Mixed = global [2 x ptr] [ptr @F1, ptr @I1], align 16

// OGCG: @Mixed = global [2 x ptr] [ptr @F1, ptr @I1], align 16
