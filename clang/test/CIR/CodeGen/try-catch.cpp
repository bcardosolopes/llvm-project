// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-mlir=cir-flat %s -o %t.flat.cir
// RUN: FileCheck --input-file=%t.flat.cir %s -check-prefix=CIR_FLAT
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

void empty_try_block_with_catch_all() {
  try {} catch (...) {}
}

// CIR: cir.func{{.*}} @_Z30empty_try_block_with_catch_allv()
// CIR:   cir.return

// CIR_FLAT: cir.func{{.*}} @_Z30empty_try_block_with_catch_allv()
// CIR_FLAT:   cir.return

// LLVM: define{{.*}} void @_Z30empty_try_block_with_catch_allv()
// LLVM:  ret void

// OGCG: define{{.*}} void @_Z30empty_try_block_with_catch_allv()
// OGCG:   ret void

void empty_try_block_with_catch_with_int_exception() {
  try {} catch (int e) {}
}

// CIR: cir.func{{.*}} @_Z45empty_try_block_with_catch_with_int_exceptionv()
// CIR:   cir.return

// CIR_FLAT: cir.func{{.*}} @_Z45empty_try_block_with_catch_with_int_exceptionv()
// CIR_FLAT:   cir.return

// LLVM: define{{.*}} void @_Z45empty_try_block_with_catch_with_int_exceptionv()
// LLVM:  ret void

// OGCG: define{{.*}} void @_Z45empty_try_block_with_catch_with_int_exceptionv()
// OGCG:   ret void

void try_catch_with_empty_catch_all() {
  int a = 1;
  try {
    return;
    ++a;
  } catch (...) {
  }
}

// CIR: %[[A_ADDR:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["a", init]
// CIR: %[[CONST_1:.*]] = cir.const #cir.int<1> : !s32i
// CIR: cir.store{{.*}} %[[CONST_1]], %[[A_ADDR]] : !s32i, !cir.ptr<!s32i
// CIR: cir.scope {
// CIR:   cir.try {
// CIR:     cir.return
// CIR:   }
// CIR: }

// CIR_FLAT: cir.func{{.*}} @_Z30try_catch_with_empty_catch_allv()
// CIR_FLAT:   %[[A_ADDR:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["a", init]
// CIR_FLAT:   %[[CONST_1:.*]] = cir.const #cir.int<1> : !s32i
// CIR_FLAT:   cir.store{{.*}} %[[CONST_1]], %[[A_ADDR]]
// CIR_FLAT:   cir.return

// LLVM:   %[[A_ADDR:.*]] = alloca i32, i64 1, align 4
// LLVM:   store i32 1, ptr %[[A_ADDR]], align 4
// LLVM:   ret void

// OGCG: %[[A_ADDR:.*]] = alloca i32, align 4
// OGCG: store i32 1, ptr %[[A_ADDR]], align 4
// OGCG: ret void

void try_catch_with_empty_catch_all_2() {
  int a = 1;
  try {
    ++a;
    return;
  } catch (...) {
  }
}

// CIR: %[[A_ADDR:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["a", init]
// CIR: %[[CONST_1:.*]] = cir.const #cir.int<1> : !s32i
// CIR: cir.store{{.*}} %[[CONST_1]], %[[A_ADDR]] : !s32i, !cir.ptr<!s32i>
// CIR: cir.scope {
// CIR:   cir.try {
// CIR:     %[[TMP_A:.*]] = cir.load{{.*}} %[[A_ADDR]] : !cir.ptr<!s32i>, !s32i
// CIR:     %[[RESULT:.*]] = cir.unary(inc, %[[TMP_A]]) nsw : !s32i, !s32i
// CIR:     cir.store{{.*}} %[[RESULT]], %[[A_ADDR]] : !s32i, !cir.ptr<!s32i>
// CIR:     cir.return
// CIR:   }
// CIR: }

// CIR_FLAT: cir.func{{.*}} @_Z32try_catch_with_empty_catch_all_2v()
// CIR_FLAT:   %[[A_ADDR:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["a", init]
// CIR_FLAT:   %[[CONST_1:.*]] = cir.const #cir.int<1> : !s32i
// CIR_FLAT:   cir.store{{.*}} %[[CONST_1]], %[[A_ADDR]]
// CIR_FLAT:   %[[TMP_A:.*]] = cir.load{{.*}} %[[A_ADDR]]
// CIR_FLAT:   %[[RESULT:.*]] = cir.unary(inc, %[[TMP_A]]) nsw
// CIR_FLAT:   cir.store{{.*}} %[[RESULT]], %[[A_ADDR]]
// CIR_FLAT:   cir.return

// LLVM:   %[[A_ADDR]] = alloca i32, i64 1, align 4
// LLVM:   store i32 1, ptr %[[A_ADDR]], align 4
// LLVM:   %[[TMP_A:.*]] = load i32, ptr %[[A_ADDR]], align 4
// LLVM:   %[[RESULT:.*]] = add nsw i32 %[[TMP_A:.*]], 1
// LLVM:   store i32 %[[RESULT]], ptr %[[A_ADDR]], align 4
// LLVM:   ret void

// OGCG: %[[A_ADDR:.*]] = alloca i32, align 4
// OGCG: store i32 1, ptr %[[A_ADDR]], align 4
// OGCG: %[[TMP_A:.*]] = load i32, ptr %[[A_ADDR]], align 4
// OGCG: %[[RESULT:.*]] = add nsw i32 %[[TMP_A]], 1
// OGCG: store i32 %[[RESULT]], ptr %[[A_ADDR]], align 4
// OGCG: ret void

void try_catch_with_alloca() {
  try {
    int a;
    int b;
    int c = a + b;
  } catch (...) {
  }
}

// CIR: cir.scope {
// CIR:   %[[A_ADDR:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["a"]
// CIR:   %[[B_ADDR:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["b"]
// CIR:   %[[C_ADDR:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["c", init]
// CIR:   cir.try {
// CIR:     %[[TMP_A:.*]] = cir.load{{.*}} %[[A_ADDR]] : !cir.ptr<!s32i>, !s32i
// CIR:     %[[TMP_B:.*]] = cir.load{{.*}} %[[B_ADDR]] : !cir.ptr<!s32i>, !s32i
// CIR:     %[[RESULT:.*]] = cir.binop(add, %[[TMP_A]], %[[TMP_B]]) nsw : !s32i
// CIR:     cir.store{{.*}} %[[RESULT]], %[[C_ADDR]] : !s32i, !cir.ptr<!s32i>
// CIR:     cir.yield
// CIR:   }
// CIR: }

// CIR_FLAT: cir.func{{.*}} @_Z21try_catch_with_allocav()
// CIR_FLAT:   %[[A_ADDR:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["a"]
// CIR_FLAT:   %[[B_ADDR:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["b"]
// CIR_FLAT:   %[[C_ADDR:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["c", init]
// CIR_FLAT:   %[[TMP_A:.*]] = cir.load{{.*}} %[[A_ADDR]]
// CIR_FLAT:   %[[TMP_B:.*]] = cir.load{{.*}} %[[B_ADDR]]
// CIR_FLAT:   %[[RESULT:.*]] = cir.binop(add, %[[TMP_A]], %[[TMP_B]]) nsw
// CIR_FLAT:   cir.store{{.*}} %[[RESULT]], %[[C_ADDR]]
// CIR_FLAT:   cir.return

// LLVM:  %[[A_ADDR:.*]] = alloca i32, i64 1, align 4
// LLVM:  %[[B_ADDR:.*]] = alloca i32, i64 1, align 4
// LLVM:  %[[C_ADDR:.*]] = alloca i32, i64 1, align 4
// LLVM:  %[[TMP_A:.*]] = load i32, ptr %[[A_ADDR]], align 4
// LLVM:  %[[TMP_B:.*]] = load i32, ptr %[[B_ADDR]], align 4
// LLVM:  %[[RESULT:.*]] = add nsw i32 %[[TMP_A]], %[[TMP_B]]
// LLVM:  store i32 %[[RESULT]], ptr %[[C_ADDR]], align 4
// LLVM:  ret void

// OGCG: %[[A_ADDR:.*]] = alloca i32, align 4
// OGCG: %[[B_ADDR:.*]] = alloca i32, align 4
// OGCG: %[[C_ADDR:.*]] = alloca i32, align 4
// OGCG: %[[TMP_A:.*]] = load i32, ptr %[[A_ADDR]], align 4
// OGCG: %[[TMP_B:.*]] = load i32, ptr %[[B_ADDR]], align 4
// OGCG: %[[RESULT:.*]] = add nsw i32 %[[TMP_A]], %[[TMP_B]]
// OGCG: store i32 %[[RESULT]], ptr %[[C_ADDR]], align 4

void function_with_noexcept() noexcept;

void calling_noexcept_function_inside_try_block() {
  try {
    function_with_noexcept();
  } catch (...) {
  }
}

// CIR: cir.scope {
// CIR:   cir.try {
// CIR:     cir.call @_Z22function_with_noexceptv() nothrow : () -> ()
// CIR:     cir.yield
// CIR:   }
// CIR: }

// CIR_FLAT: cir.func{{.*}} @_Z42calling_noexcept_function_inside_try_blockv()
// CIR_FLAT:   cir.call @_Z22function_with_noexceptv() nothrow
// CIR_FLAT:   cir.return

// LLVM:   call void @_Z22function_with_noexceptv()
// LLVM:   ret void

// OGCG: call void @_Z22function_with_noexceptv()
// OGCG: ret void

void may_throw();

int catch_int_with_return() {
  try { may_throw(); }
  catch (int e) { return e; }
  return 0;
}

// CIR: cir.func{{.*}} @_Z21catch_int_with_returnv() -> {{.*}}!s32i
// CIR:   %[[RETVAL:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["__retval"]
// CIR:   cir.scope {
// CIR:     %[[E:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["e"]
// CIR:     cir.try {
// CIR:       cir.call exception @_Z9may_throwv()
// CIR:       cir.yield
// CIR:     } catch [type #cir.global_view<@_ZTIi> : !cir.ptr<!u8i>] {
// CIR:       %[[PARAM:.*]] = cir.catch_param : !cir.ptr<!s32i>
// CIR:       %[[V1:.*]] = cir.load{{.*}} %[[PARAM]]
// CIR:       cir.store{{.*}} %[[V1]], %[[E]]
// CIR:       %[[V2:.*]] = cir.load{{.*}} %[[E]]
// CIR:       cir.store %[[V2]], %[[RETVAL]]
// CIR:       %[[V3:.*]] = cir.load %[[RETVAL]]
// CIR:       cir.return %[[V3]] : !s32i
// CIR:     } unwind {
// CIR:       cir.resume
// CIR:     }
// CIR:   }

// CIR_FLAT: cir.func{{.*}} @_Z21catch_int_with_returnv() -> {{.*}}!s32i
// CIR_FLAT:   %[[E:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["e"]
// CIR_FLAT:   %[[RETVAL:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["__retval"]
// CIR_FLAT:   cir.try_call @_Z9may_throwv()
// CIR_FLAT:   %[[PARAM:.*]] = cir.catch_param begin
// CIR_FLAT:   %[[V1:.*]] = cir.load{{.*}} %[[PARAM]]
// CIR_FLAT:   cir.store{{.*}} %[[V1]], %[[E]]
// CIR_FLAT:   %[[V2:.*]] = cir.load{{.*}} %[[E]]
// CIR_FLAT:   cir.store %[[V2]], %[[RETVAL]]
// CIR_FLAT:   %[[V3:.*]] = cir.load %[[RETVAL]]
// CIR_FLAT:   cir.return %[[V3]] : !s32i

// LLVM: define{{.*}} noundef i32 @_Z21catch_int_with_returnv()
// LLVM:   invoke void @_Z9may_throwv()
// LLVM:   landingpad { ptr, i32 }
// LLVM:     catch ptr @_ZTIi
// LLVM:   call ptr @__cxa_begin_catch(
// LLVM:   ret i32

// OGCG: define{{.*}} i32 @_Z21catch_int_with_returnv()
// OGCG:   invoke void @_Z9may_throwv()
// OGCG:   landingpad { ptr, i32 }
// OGCG:     catch ptr @_ZTIi
// OGCG:   call ptr @__cxa_begin_catch(
// OGCG:   call void @__cxa_end_catch()
// OGCG:   ret i32

struct Ex { int code; };

int catch_ref_with_return() {
  try { may_throw(); }
  catch (Ex &e) { return e.code; }
  return 0;
}

// CIR: cir.func{{.*}} @_Z21catch_ref_with_returnv() -> {{.*}}!s32i
// CIR:   %[[RETVAL:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["__retval"]
// CIR:   cir.scope {
// CIR:     %[[E:.*]] = cir.alloca !cir.ptr<!rec_Ex>, !cir.ptr<!cir.ptr<!rec_Ex>>, ["e", const]
// CIR:     cir.try {
// CIR:       cir.call exception @_Z9may_throwv()
// CIR:       cir.yield
// CIR:     } catch [type #cir.global_view<@_ZTI2Ex> : !cir.ptr<!u8i>] {
// CIR:       %[[PARAM:.*]] = cir.catch_param : !cir.ptr<!rec_Ex>
// CIR:       cir.store{{.*}} %[[PARAM]], %[[E]] : !cir.ptr<!rec_Ex>, !cir.ptr<!cir.ptr<!rec_Ex>>
// CIR:       %[[ELOAD:.*]] = cir.load %[[E]] : !cir.ptr<!cir.ptr<!rec_Ex>>, !cir.ptr<!rec_Ex>
// CIR:       %[[CODE:.*]] = cir.get_member %[[ELOAD]][0] {name = "code"} : !cir.ptr<!rec_Ex> -> !cir.ptr<!s32i>
// CIR:       %[[V1:.*]] = cir.load{{.*}} %[[CODE]] : !cir.ptr<!s32i>, !s32i
// CIR:       cir.store %[[V1]], %[[RETVAL]]
// CIR:       %[[V2:.*]] = cir.load %[[RETVAL]]
// CIR:       cir.return %[[V2]] : !s32i
// CIR:     } unwind {
// CIR:       cir.resume
// CIR:     }
// CIR:   }

// CIR_FLAT: cir.func{{.*}} @_Z21catch_ref_with_returnv() -> {{.*}}!s32i
// CIR_FLAT:   %[[E:.*]] = cir.alloca !cir.ptr<!rec_Ex>, !cir.ptr<!cir.ptr<!rec_Ex>>, ["e"]
// CIR_FLAT:   %[[RETVAL:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["__retval"]
// CIR_FLAT:   cir.try_call @_Z9may_throwv()
// CIR_FLAT:   %[[PARAM:.*]] = cir.catch_param begin
// CIR_FLAT:   cir.store{{.*}} %[[PARAM]], %[[E]]
// CIR_FLAT:   %[[ELOAD:.*]] = cir.load %[[E]]
// CIR_FLAT:   %[[CODE:.*]] = cir.get_member %[[ELOAD]][0] {name = "code"}
// CIR_FLAT:   %[[V1:.*]] = cir.load{{.*}} %[[CODE]]
// CIR_FLAT:   cir.store %[[V1]], %[[RETVAL]]
// CIR_FLAT:   %[[V2:.*]] = cir.load %[[RETVAL]]
// CIR_FLAT:   cir.return %[[V2]] : !s32i

// LLVM: define{{.*}} noundef i32 @_Z21catch_ref_with_returnv()
// LLVM:   invoke void @_Z9may_throwv()
// LLVM:   landingpad { ptr, i32 }
// LLVM:     catch ptr @_ZTI2Ex
// LLVM:   call ptr @__cxa_begin_catch(
// LLVM:   getelementptr inbounds nuw %struct.Ex
// LLVM:   ret i32

// OGCG: define{{.*}} i32 @_Z21catch_ref_with_returnv()
// OGCG:   invoke void @_Z9may_throwv()
// OGCG:   landingpad { ptr, i32 }
// OGCG:     catch ptr @_ZTI2Ex
// OGCG:   call ptr @__cxa_begin_catch(
// OGCG:   getelementptr inbounds nuw %struct.Ex
// OGCG:   call void @__cxa_end_catch()
// OGCG:   ret i32
