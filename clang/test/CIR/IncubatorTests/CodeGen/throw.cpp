// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -mconstructor-aliases -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -mconstructor-aliases -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

double d(int a, int b) {
   if (b == 0)
      throw "Division by zero condition!";
   return (a/b);
}

//      CIR: cir.if
// CIR-NEXT:   %[[ADDR:.*]] = cir.alloc.exception 8
// CIR-NEXT:   %[[STR:.*]] = cir.get_global @".str" : !cir.ptr<!cir.array<!s8i x 28>>
// CIR-NEXT:   %[[STR_ADD:.*]] = cir.cast array_to_ptrdecay %[[STR]] : !cir.ptr<!cir.array<!s8i x 28>> -> !cir.ptr<!s8i>
// CIR-NEXT:   cir.store{{.*}} %[[STR_ADD]], %[[ADDR]] : !cir.ptr<!s8i>, !cir.ptr<!cir.ptr<!s8i>>
// CIR-NEXT:   cir.throw %[[ADDR]] : !cir.ptr<!cir.ptr<!s8i>>, @_ZTIPKc
// CIR-NEXT:   cir.unreachable
// CIR-NEXT: }

// LLVM: %[[ADDR:.*]] = call ptr @__cxa_allocate_exception(i64 8)
// LLVM: store ptr @.str, ptr %[[ADDR]], align 16
// LLVM: call void @__cxa_throw(ptr %[[ADDR]], ptr @_ZTIPKc, ptr null)
// LLVM: unreachable

struct S {
  S() {}
};

void refoo1() {
  int r = 1;
  try {
    S s;
    throw;
  } catch (...) {
    ++r;
  }
}

// CIR-LABEL: @_Z6refoo1v()
// CIR:   %[[V0:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["r", init] {alignment = 4 : i64}
// CIR:   %[[V1:.*]] = cir.const #cir.int<1> : !s32i
// CIR:   cir.store{{.*}} %[[V1]], %[[V0]] : !s32i, !cir.ptr<!s32i>
// CIR:   cir.scope {
// CIR:     %[[V2:.*]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["s", init] {alignment = 1 : i64}
// CIR:     cir.try {
// CIR:       cir.call exception @_ZN1SC2Ev(%[[V2]]) : (!cir.ptr<!rec_S>) -> ()
// CIR:       cir.call exception @__cxa_rethrow() : () -> ()
// CIR:       cir.unreachable
// CIR:     } catch all {
// CIR:       %[[V3:.*]] = cir.catch_param : !cir.ptr<!void>
// CIR:       %[[V4:.*]] = cir.load{{.*}} %[[V0]] : !cir.ptr<!s32i>, !s32i
// CIR:       %[[V5:.*]] = cir.unary(inc, %[[V4]]) nsw : !s32i, !s32i
// CIR:       cir.store{{.*}} %[[V5]], %[[V0]] : !s32i, !cir.ptr<!s32i>
// CIR:       cir.yield
// CIR:     }
// CIR:   }
// CIR:   cir.return
// CIR: }

// LLVM: define dso_local void @_Z6refoo1v()
// LLVM:   %[[V1:.*]] = alloca %struct.S, i64 1, align 1
// LLVM:   %[[V2:.*]] = alloca i32, i64 1, align 4
// LLVM:   store i32 1, ptr %[[V2]], align 4
// LLVM:   invoke void @_ZN1SC2Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[V1]])
// LLVM:           to label %[[B3:.*]] unwind label %[[B5:.*]]
// LLVM: [[B3]]:
// LLVM:   invoke void @__cxa_rethrow()
// LLVM:           to label %[[B4:.*]] unwind label %[[B5]]
// LLVM: [[B4]]:
// LLVM:   unreachable
// LLVM: [[B5]]:
// LLVM:   %[[V6:.*]] = landingpad { ptr, i32 }
// LLVM:           catch ptr null
// LLVM:   %[[V7:.*]] = extractvalue { ptr, i32 } %[[V6]], 0
// LLVM:   %[[V8:.*]] = call ptr @__cxa_begin_catch(ptr %[[V7]])
// LLVM:   %[[V9:.*]] = load i32, ptr %[[V2]], align 4
// LLVM:   %[[V10:.*]] = add nsw i32 %[[V9]], 1
// LLVM:   store i32 %[[V10]], ptr %[[V2]], align 4
// LLVM:   call void @__cxa_end_catch()

void refoo2() {
  int r = 1;
  try {
    for (int i = 0; i < 5; i++) {
      S s;
      throw;
    }
    S s;
  } catch (...) {
    ++r;
  }
}

// CIR-LABEL: @_Z6refoo2v()
// CIR:   %[[V0:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["r", init] {alignment = 4 : i64}
// CIR:   %[[V1:.*]] = cir.const #cir.int<1> : !s32i
// CIR:   cir.store{{.*}} %[[V1]], %[[V0]] : !s32i, !cir.ptr<!s32i>
// CIR:   cir.scope {
// CIR:     %[[V2:.*]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["s", init] {alignment = 1 : i64}
// CIR:     cir.try {
// CIR:       cir.scope {
// CIR:         %[[V3:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i", init] {alignment = 4 : i64}
// CIR:         %[[V4:.*]] = cir.const #cir.int<0> : !s32i
// CIR:         cir.store{{.*}} %[[V4]], %[[V3]] : !s32i, !cir.ptr<!s32i>
// CIR:         cir.for : cond {
// CIR:           %[[V5:.*]] = cir.load{{.*}} %[[V3]] : !cir.ptr<!s32i>, !s32i
// CIR:           %[[V6:.*]] = cir.const #cir.int<5> : !s32i
// CIR:           %[[V7:.*]] = cir.cmp(lt, %[[V5]], %[[V6]]) : !s32i, !cir.bool
// CIR:           cir.condition(%[[V7]])
// CIR:         } body {
// CIR:           cir.scope {
// CIR:             %[[V5:.*]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["s", init] {alignment = 1 : i64}
// CIR:             cir.call exception @_ZN1SC2Ev(%[[V5]]) : (!cir.ptr<!rec_S>) -> ()
// CIR:             cir.call exception @__cxa_rethrow() : () -> ()
// CIR:             cir.unreachable
// CIR:           }
// CIR:           cir.yield
// CIR:         } step {
// CIR:           %[[V5:.*]] = cir.load{{.*}} %[[V3]] : !cir.ptr<!s32i>, !s32i
// CIR:           %[[V6:.*]] = cir.unary(inc, %[[V5]]) nsw : !s32i, !s32i
// CIR:           cir.store{{.*}} %[[V6]], %[[V3]] : !s32i, !cir.ptr<!s32i>
// CIR:           cir.yield
// CIR:         }
// CIR:       }
// CIR:       cir.call exception @_ZN1SC2Ev(%[[V2]]) : (!cir.ptr<!rec_S>) -> ()
// CIR:       cir.yield
// CIR:     } catch all {
// CIR:       %[[V3:.*]] = cir.catch_param : !cir.ptr<!void>
// CIR:       %[[V4:.*]] = cir.load{{.*}} %[[V0]] : !cir.ptr<!s32i>, !s32i
// CIR:       %[[V5:.*]] = cir.unary(inc, %[[V4]]) nsw : !s32i, !s32i
// CIR:       cir.store{{.*}} %[[V5]], %[[V0]] : !s32i, !cir.ptr<!s32i>
// CIR:       cir.yield
// CIR:     }
// CIR:   }
// CIR:   cir.return
// CIR: }

// LLVM: {{.*}}:
// LLVM:   invoke void @_ZN1SC2Ev(ptr noundef nonnull align 1 dereferenceable(1) %[[V3:.*]])
// LLVM:           to label %[[B9:.*]] unwind label %[[B13:.*]]
// LLVM: [[B9]]:
// LLVM:   invoke void @__cxa_rethrow()
// LLVM:           to label %[[B10:.*]] unwind label %[[B13]]
// LLVM: [[B10]]:
// LLVM:   unreachable
// LLVM: {{.*}}:
// LLVM:   invoke void @_ZN1SC2Ev(ptr {{.*}})
// LLVM:           to label %[[B12:.*]] unwind label %[[B13]]
// LLVM: [[B12]]:
// LLVM:   br label {{.*}}
// LLVM: [[B13]]:
// LLVM:   %[[V14:.*]] = landingpad { ptr, i32 }
// LLVM:           catch ptr null
// LLVM:   %[[V15:.*]] = extractvalue { ptr, i32 } %[[V14]], 0
// LLVM:   %[[V16:.*]] = call ptr @__cxa_begin_catch(ptr %[[V15]])
// LLVM:   %[[V17:.*]] = load i32, ptr {{.*}}, align 4
// LLVM:   %[[V18:.*]] = add nsw i32 %[[V17]], 1
// LLVM:   store i32 %[[V18]], ptr {{.*}}, align 4
// LLVM:   call void @__cxa_end_catch()
// LLVM:   br label {{.*}}

void refoo3() {
  int r = 1;
  try {
    throw;
    S s;
  } catch (...) {
    ++r;
  }
}

// CIR-LABEL: @_Z6refoo3v()
// CIR:   %[[V0:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["r", init] {alignment = 4 : i64}
// CIR:   %[[V1:.*]] = cir.const #cir.int<1> : !s32i
// CIR:   cir.store{{.*}} %[[V1]], %[[V0]] : !s32i, !cir.ptr<!s32i>
// CIR:   cir.scope {
// CIR:     cir.try {
// CIR:       cir.call exception @__cxa_rethrow() : () -> ()
// CIR:       cir.unreachable
// CIR:     } catch all {
// CIR:       %[[V3:.*]] = cir.catch_param : !cir.ptr<!void>
// CIR:       %[[V4:.*]] = cir.load{{.*}} %[[V0]] : !cir.ptr<!s32i>, !s32i
// CIR:       %[[V5:.*]] = cir.unary(inc, %[[V4]]) nsw : !s32i, !s32i
// CIR:       cir.store{{.*}} %[[V5]], %[[V0]] : !s32i, !cir.ptr<!s32i>
// CIR:       cir.yield
// CIR:     }
// CIR:   }
// CIR:   cir.return
// CIR: }

// LLVM:  invoke void @__cxa_rethrow()
// LLVM:          to label %[[B2:.*]] unwind label %[[B3:.*]]
// LLVM: [[B2]]:
// LLVM:  unreachable
// LLVM-NOT: invoke void @_ZN1SC2Ev
// LLVM: [[B3]]:
// LLVM:  landingpad { ptr, i32 }
// LLVM:          catch ptr null

void refoo4() {
  try {
    for (int i = 0; i < 5; i++) {
      throw;
      throw;
      S s;
      i++;
    }
  } catch (...) {
    int r = 1;
  }
}

// CIR-LABEL: @_Z6refoo4v
// CIR: cir.call exception @__cxa_rethrow() : () -> ()
// CIR-NEXT: unreachable
// CIR-NOT: cir.call exception @_ZN1SC2Ev

// LLVM: invoke void @__cxa_rethrow
// LLVM: unreachable
// LLVM-NOT: invoke void @_ZN1SC2Ev

void statements() {
  throw 0;
  123 + 456;
}

// CIR:      cir.func {{.*}} @_Z10statementsv()
// CIR-NEXT:   %[[V0:.*]] = cir.alloc.exception 4 -> !cir.ptr<!s32i>
// CIR-NEXT:   %[[V1:.*]] = cir.const #cir.int<0> : !s32i
// CIR-NEXT:   cir.store align(16) %[[V1]], %[[V0]] : !s32i, !cir.ptr<!s32i>
// CIR-NEXT:   cir.throw %[[V0]] : !cir.ptr<!s32i>, @_ZTIi
// CIR-NEXT:   cir.unreachable
// CIR-NEXT: }

// LLVM: call void @__cxa_throw
// LLVM: unreachable

void paren_expr() { (throw 0, 123 + 456); }

// CIR:       cir.func {{.*}} @_Z10paren_exprv()
// CIR-NEXT:   %[[V0:.*]] = cir.alloc.exception 4 -> !cir.ptr<!s32i>
// CIR-NEXT:   %[[V1:.*]] = cir.const #cir.int<0> : !s32i
// CIR-NEXT:   cir.store align(16) %[[V1]], %[[V0]] : !s32i, !cir.ptr<!s32i>
// CIR-NEXT:   cir.throw %[[V0]] : !cir.ptr<!s32i>, @_ZTIi
// CIR-NEXT:   cir.unreachable
// CIR-NEXT: ^bb1:
// CIR-NEXT:   %[[V2:.*]] = cir.const #cir.int<123> : !s32i
// CIR-NEXT:   %[[V3:.*]] = cir.const #cir.int<456> : !s32i
// CIR-NEXT:   %[[V4:.*]] = cir.binop(add, %[[V2]], %[[V3]]) nsw : !s32i
// CIR-NEXT:   cir.return
// CIR-NEXT: }

// LLVM: call void @__cxa_throw
// LLVM: unreachable

int ternary_throw1(bool condition, int x) {
  return condition ? throw x : x;
}

// CIR:      cir.func {{.*}} @_Z14ternary_throw1bi(%arg0: !cir.bool
// CIR:        %[[V0:.*]] = cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["condition", init] {alignment = 1 : i64}
// CIR:        %[[V1:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init] {alignment = 4 : i64}
// CIR:        %[[CLEANUP:.*]] = cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"] {alignment = 1 : i64}
// CIR:        cir.store %arg0, %[[V0]] : !cir.bool, !cir.ptr<!cir.bool>
// CIR:        cir.store %arg1, %[[V1]] : !s32i, !cir.ptr<!s32i>
// CIR:        %[[V4:.*]] = cir.load align(1) %[[V0]] : !cir.ptr<!cir.bool>, !cir.bool
// CIR:        %[[F:.*]] = cir.const #false
// CIR:        cir.store align(1) %[[F]], %[[CLEANUP]] : !cir.bool, !cir.ptr<!cir.bool>
// CIR:        %[[V5:.*]] = cir.ternary(%[[V4]], true {
// CIR:          %[[EXN:.*]] = cir.alloc.exception 4 -> !cir.ptr<!s32i>
// CIR:          %[[T:.*]] = cir.const #true
// CIR:          cir.store align(1) %[[T]], %[[CLEANUP]] : !cir.bool, !cir.ptr<!cir.bool>
// CIR:          %[[V7:.*]] = cir.load align(4) %[[V1]] : !cir.ptr<!s32i>, !s32i
// CIR:          cir.store align(16) %[[V7]], %[[EXN]] : !s32i, !cir.ptr<!s32i>
// CIR:          cir.throw %[[EXN]] : !cir.ptr<!s32i>, @_ZTIi
// CIR:          cir.unreachable
// CIR:        }, false {
// CIR:          %[[V6:.*]] = cir.load align(4) %[[V1]] : !cir.ptr<!s32i>, !s32i
// CIR:          cir.yield %[[V6]] : !s32i
// CIR:        }) : (!cir.bool) -> !s32i
// CIR:        cir.return %[[V5]] : !s32i

// LLVM: @_Z14ternary_throw1bi
// LLVM:   %[[V3:.*]] = alloca i8, i64 1, align 1
// LLVM:   %[[V4:.*]] = alloca i32, i64 1, align 4
// LLVM:   %[[V5:.*]] = alloca i8, i64 1, align 1
// LLVM:   %[[V6:.*]] = zext i1 %[[V0:.*]] to i8
// LLVM:   store i8 %[[V6]], ptr %[[V3]], align 1
// LLVM:   store i32 %[[V1:.*]], ptr %[[V4]], align 4
// LLVM:   %[[V7:.*]] = load i8, ptr %[[V3]], align 1
// LLVM:   %[[V8:.*]] = trunc i8 %[[V7]] to i1
// LLVM:   store i8 0, ptr %[[V5]], align 1
// LLVM:   br i1 %[[V8]], label %[[B9:.*]], label %[[B12:.*]]
// LLVM: [[B9]]:
// LLVM:   %[[V10:.*]] = call ptr @__cxa_allocate_exception(i64 4)
// LLVM:   store i8 1, ptr %[[V5]], align 1
// LLVM:   %[[V11:.*]] = load i32, ptr %[[V4]], align 4
// LLVM:   store i32 %[[V11]], ptr %[[V10]], align 16
// LLVM:   call void @__cxa_throw(ptr %[[V10]], ptr @_ZTIi, ptr null)
// LLVM:   unreachable
// LLVM: [[B12]]:
// LLVM:   %[[V13:.*]] = load i32, ptr %[[V4]], align 4
// LLVM:   br label %[[B14:.*]]
// LLVM: [[B14]]:
// LLVM:   %[[V15:.*]] = phi i32 [ %[[V13]], %[[B12]] ]
// LLVM:   ret i32 %[[V15]]

int ternary_throw2(bool condition, int x) {
  return condition ? x : throw x;
}

// LLVM: @_Z14ternary_throw2bi
// LLVM:   %[[V3:.*]] = alloca i8, i64 1, align 1
// LLVM:   %[[V4:.*]] = alloca i32, i64 1, align 4
// LLVM:   %[[V5:.*]] = alloca i8, i64 1, align 1
// LLVM:   %[[V6:.*]] = zext i1 %[[V0:.*]] to i8
// LLVM:   store i8 %[[V6]], ptr %[[V3]], align 1
// LLVM:   store i32 %[[V1]], ptr %[[V4]], align 4
// LLVM:   %[[V7:.*]] = load i8, ptr %[[V3]], align 1
// LLVM:   %[[V8:.*]] = trunc i8 %[[V7]] to i1
// LLVM:   store i8 0, ptr %[[V5]], align 1
// LLVM:   br i1 %[[V8]], label %[[B9:.*]], label %[[B11:.*]]
// LLVM: [[B9]]:
// LLVM:   %[[V10:.*]] = load i32, ptr %[[V4]], align 4
// LLVM:   br label %[[B14:.*]]
// LLVM: [[B11]]:
// LLVM:   %[[V12:.*]] = call ptr @__cxa_allocate_exception(i64 4)
// LLVM:   store i8 1, ptr %[[V5]], align 1
// LLVM:   %[[V13:.*]] = load i32, ptr %[[V4]], align 4
// LLVM:   store i32 %[[V13]], ptr %[[V12]], align 16
// LLVM:   call void @__cxa_throw(ptr %[[V12]], ptr @_ZTIi, ptr null)
// LLVM:   unreachable
// LLVM: [[B14:.*]]:
// LLVM:   %[[V15:.*]] = phi i32 [ %[[V10]], %[[B9]] ]
// LLVM:   ret i32 %[[V15]]
