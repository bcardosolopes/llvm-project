// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=shift-base,shift-exponent \
// RUN:   -fsanitize-recover=shift-base,shift-exponent -o - %s | FileCheck %s --check-prefix=RECOVER
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=shift-base,shift-exponent -o - %s | FileCheck %s --check-prefix=ABORT
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=shift-base,shift-exponent -fsanitize-trap=shift-base,shift-exponent \
// RUN:   -fsanitize-recover=shift-base,shift-exponent -o - %s | FileCheck %s --check-prefix=TRAP

// RECOVER-LABEL: define {{.*}} @_Z12lsh_overflowii
// RECOVER: %[[INBOUNDS:.*]] = icmp ule i32 %[[RHS:.*]], 31
// RECOVER: %[[SHIFTW:.*]] = sub nuw nsw i32 31, %[[RHS]]
// RECOVER: %[[SHIFTED:.*]] = lshr i32 %[[LHS:.*]], %[[SHIFTW]]
// RECOVER: %[[SHIFTED_NOT_SIGN:.*]] = lshr i32 %[[SHIFTED]], 1
// RECOVER: %[[NOOV:.*]] = icmp eq i32 %[[SHIFTED_NOT_SIGN]], 0
// RECOVER: %[[VALID_BASE:.*]] = phi i1 [ true, %{{.*}} ], [ %[[NOOV]], %{{.*}} ]
// RECOVER: %[[VALID:.*]] = and i1 %[[VALID_BASE]], %[[INBOUNDS]]
// RECOVER: br i1 %[[VALID]], label %{{.*}}, label %{{.*}}, !prof ![[PROF:[0-9]+]], !nosanitize ![[NOSAN:[0-9]+]]
// RECOVER: %[[A64:.*]] = zext i32 %[[LHS]] to i64
// RECOVER: %[[B64:.*]] = zext i32 %[[RHS]] to i64
// RECOVER: call void @__ubsan_handle_shift_out_of_bounds(ptr {{.*}}, i64 %[[A64]], i64 %[[B64]]) #[[CALLATTR:[0-9]+]], !nosanitize ![[NOSAN]]
// RECOVER: ret i32

// ABORT-LABEL: define {{.*}} @_Z12lsh_overflowii
// ABORT: call void @__ubsan_handle_shift_out_of_bounds_abort(ptr {{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[CALLATTR:[0-9]+]]
// ABORT-NEXT: unreachable

// TRAP-LABEL: define {{.*}} @_Z12lsh_overflowii
// TRAP: call void @llvm.ubsantrap(i8 20)
// TRAP-NEXT: unreachable

// RECOVER-LABEL: define {{.*}} @_Z11rsh_checkedii
// RECOVER: %[[LHS2:.*]] = load i32
// RECOVER: %[[RHS2:.*]] = load i32
// RECOVER: %[[INBOUNDS2:.*]] = icmp ule i32 %[[RHS2]], 31
// RECOVER: call void @__ubsan_handle_shift_out_of_bounds(ptr {{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[CALLATTR]]
// RECOVER: %[[RET2:.*]] = ashr i32 %[[LHS2]], %[[RHS2]]
// RECOVER: ret i32 %{{.*}}
// RECOVER: attributes #[[CALLATTR]] = { nomerge nounwind }

// ABORT-LABEL: define {{.*}} @_Z11rsh_checkedii
// ABORT: call void @__ubsan_handle_shift_out_of_bounds_abort(ptr {{.*}}, i64 %{{.*}}, i64 %{{.*}}) #[[CALLATTR]]
// ABORT-NEXT: unreachable
// ABORT: attributes #[[CALLATTR]] = { nomerge noreturn nounwind }

// TRAP-LABEL: define {{.*}} @_Z11rsh_checkedii
// TRAP: call void @llvm.ubsantrap(i8 20)
// TRAP-NEXT: unreachable

int lsh_overflow(int a, int b) {
  return a << b;
}

int rsh_checked(int a, int b) {
  return a >> b;
}
