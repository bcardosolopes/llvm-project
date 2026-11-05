// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O1 -disable-llvm-passes -fclangir -emit-llvm %s -o %t.raw.ll
// RUN: FileCheck --input-file=%t.raw.ll --check-prefix=RAWLLVM %s

int produce_int();
void blackbox(const int &);
void consume(int);

void local_const_int() {
  const int x = produce_int();
}

// CIR-LABEL: @_Z15local_const_intv
// CIR:   %{{.+}} = cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init, const]
// CIR: }

void param_const_int(const int x) {}

// CIR-LABEL: @_Z15param_const_inti
// CIR:  %{{.+}} = cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init, const]
// CIR: }

void local_constexpr_int() {
  constexpr int x = 42;
  blackbox(x);
}

// CIR-LABEL: @_Z19local_constexpr_intv
// CIR:   %{{.+}} = cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init, const]
// CIR: }

void local_reference() {
  int x = 0;
  int &r = x;
}

// CIR-LABEL: @_Z15local_referencev
// CIR:   %{{.+}} = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["r", init, const]
// CIR: }

struct Foo {
  int a;
  int b;
};

Foo produce_foo();

void local_const_struct() {
  const Foo x = produce_foo();
}

// CIR-LABEL: @_Z18local_const_structv
// CIR:   %{{.+}} = cir.alloca !rec_Foo, !cir.ptr<!rec_Foo>, ["x", init, const]
// CIR: }

[[clang::optnone]]
int local_const_load_store() {
  const int x = produce_int();
  int y = x;
  return y;
}

// CIR-LABEL: @_Z22local_const_load_storev
// CIR: %{{.+}} = cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init, const] {alignment = 4 : i64}
// CIR: }

// LLVM-LABEL: @_Z22local_const_load_storev
//      LLVM: %[[#INIT:]] = call noundef i32 @_Z11produce_intv()
// LLVM-NEXT: store i32 %[[#INIT]], ptr %[[#SLOT:]], align 4, !tbaa {{.*}}
//      LLVM: %{{.+}} = load i32, ptr %[[#SLOT]], align 4, !tbaa {{.*}}
// LLVM: }

int local_const_optimize() {
  const int x = produce_int();
  blackbox(x);
  blackbox(x);
  return x;
}

// LLVM-LABEL: @_Z20local_const_optimizev()
// LLVM-NEXT:    %[[#slot:]] = alloca i32, align 4
// LLVM-NEXT:    call void @llvm.lifetime.start.p0(ptr nonnull %[[#slot]])
// LLVM-NEXT:    %[[#init:]] = tail call noundef i32 @_Z11produce_intv()
// LLVM-NEXT:    store i32 %[[#init]], ptr %[[#slot]], align 4, !tbaa {{.*}}
// LLVM-NEXT:    call void @_Z8blackboxRKi(ptr {{[^%]*}}%[[#slot]])
// LLVM-NEXT:    call void @_Z8blackboxRKi(ptr {{[^%]*}}%[[#slot]])
//      LLVM:    ret i32
// LLVM-NEXT:  }

int local_scoped_const() {
  {
    const int x = produce_int();
    blackbox(x);
    return x;
  }
}

// CIR-LABEL: @_Z18local_scoped_constv()
//      CIR:    cir.scope {
// CIR-NEXT:      %[[#x_slot:]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init, const]
// CIR-NEXT:      cir.lifetime.start %[[#x_slot]] : !cir.ptr<!s32i>
// CIR-NEXT:      %[[#init:]] = cir.call @_Z11produce_intv() : () -> !s32i
// CIR-NEXT:      cir.store{{.*}} %[[#init]], %[[#x_slot]] : !s32i, !cir.ptr<!s32i>
// CIR-NEXT:      cir.call @_Z8blackboxRKi(%[[#x_slot]]) : (!cir.ptr<!s32i>) -> ()
// CIR-NEXT:      %[[#x_reload:]] = cir.load{{.*}} %[[#x_slot]] : !cir.ptr<!s32i>, !s32i
// CIR-NEXT:      cir.lifetime.end %[[#x_slot]] : !cir.ptr<!s32i>
// CIR-NEXT:      cir.return %[[#x_reload]] : !s32i
//      CIR:    }
//      CIR:  }

// LLVM-LABEL: @_Z18local_scoped_constv()
// LLVM-NEXT:    %[[#x_slot:]] = alloca i32, align 4
// LLVM-NEXT:    call void @llvm.lifetime.start.p0(ptr nonnull %[[#x_slot]])
// LLVM-NEXT:    %[[#init:]] = tail call noundef i32 @_Z11produce_intv()
// LLVM-NEXT:    store i32 %[[#init]], ptr %[[#x_slot]], align 4, !tbaa {{.*}}
// LLVM-NEXT:    call void @_Z8blackboxRKi(ptr {{[^%]*}}%[[#x_slot]])
//      LLVM:    ret i32
// LLVM-NEXT:  }

void local_const_in_loop() {
  for (int i = 0; i < 10; ++i) {
    const int x = produce_int();
    blackbox(x);
    consume(x);
  }
}

// CIR-LABEL: @_Z19local_const_in_loopv
//      CIR:    cir.scope {
//      CIR:      cir.for : cond {
//      CIR:      } body {
// CIR-NEXT:        cir.scope {
// CIR-NEXT:          %[[#x_slot:]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init, const]
// CIR-NEXT:          cir.lifetime.start %[[#x_slot]] : !cir.ptr<!s32i>
// CIR-NEXT:          %[[#init:]] = cir.call @_Z11produce_intv() : () -> !s32i
// CIR-NEXT:          cir.store{{.*}} %[[#init]], %[[#x_slot]] : !s32i, !cir.ptr<!s32i>
// CIR-NEXT:          cir.call @_Z8blackboxRKi(%[[#x_slot]]) : (!cir.ptr<!s32i>) -> ()
// CIR-NEXT:          %[[#x_reload:]] = cir.load{{.*}} %[[#x_slot]] : !cir.ptr<!s32i>, !s32i
// CIR-NEXT:          cir.call @_Z7consumei(%[[#x_reload]]) : (!s32i) -> ()
// CIR-NEXT:          cir.lifetime.end %[[#x_slot]] : !cir.ptr<!s32i>
// CIR-NEXT:        }
// CIR-NEXT:        cir.yield
// CIR-NEXT:      } step {
//      CIR:      }
//      CIR:    }
//      CIR:    cir.return
// CIR-NEXT:  }

// A const local declared in the loop *body* is re-initialized on each
// iteration, so its storage is not invariant across the pointer's lifetime.
// HoistAllocas must drop the "const" flag and must NOT emit cir.invariant_group
// / llvm.launder.invariant.group + !invariant.group metadata: that metadata is
// folded to undef by SROA/EarlyCSE and miscompiles reads of the value (see
// HoistAllocas::processConstAlloca).
// LLVM-LABEL: @_Z19local_const_in_loopv()
// LLVM-NOT:     @llvm.launder.invariant.group
//      LLVM:    call {{.*}}@_Z11produce_intv()
// LLVM-NOT:     invariant.group
//      LLVM:    call void @_Z8blackboxRKi(
//      LLVM:    call void @_Z7consumei(
// LLVM-NOT:     invariant.group
//      LLVM:  }

void local_const_in_while_condition() {
  while (const int x = produce_int()) {
    blackbox(x);
  }
}

void local_const_in_while_condition_break() {
  while (const int x = produce_int()) {
    break;
  }
}

void local_const_in_for_condition() {
  for (; const int x = produce_int();) {
    blackbox(x);
  }
}

void local_const_in_for_condition_break() {
  for (; const int x = produce_int();) {
    break;
  }
}

// LLVM-LABEL: @_Z30local_const_in_while_conditionv()
//      LLVM:    %[[#x_slot:]] = alloca i32, align 4
//      LLVM:    %[[#init:]] = tail call noundef i32 @_Z11produce_intv()
// LLVM-NEXT:    store i32 %[[#init]], ptr %[[#x_slot]], align 4
// LLVM-NEXT:    %[[loop_cond:.+]] = icmp eq i32 %[[#init]], 0
// LLVM-NEXT:    br i1 %[[loop_cond]], label %{{.+}}, label %[[loop_body:.+]]
//      LLVM:  [[loop_body]]:
// LLVM-NEXT:    call void @_Z8blackboxRKi(ptr {{[^%]*}}%[[#x_slot]])
//      LLVM:    %[[#next:]] = call noundef i32 @_Z11produce_intv()
// LLVM-NEXT:    store i32 %[[#next]], ptr %[[#x_slot]], align 4
// LLVM-NEXT:    %[[cond:.+]] = icmp eq i32 %[[#next]], 0
// LLVM-NEXT:    br i1 %[[cond]], label %{{.+}}, label %[[loop_body]]
//      LLVM:  }

// RAWLLVM-LABEL: @_Z30local_const_in_while_conditionv()
//      RAWLLVM:    %[[X:[0-9]+]] = alloca i32, i64 1, align 4
//      RAWLLVM:    br label %[[COND:[^,]+]]
//      RAWLLVM:  [[COND]]:
// RAWLLVM-NEXT:    call void @llvm.lifetime.start.p0(ptr %[[X]])
//      RAWLLVM:    %[[INIT:[0-9]+]] = call noundef i32 @_Z11produce_intv()
//      RAWLLVM:    store i32 %[[INIT]], ptr %[[X]], align 4
//      RAWLLVM:    %[[LOAD:[0-9]+]] = load i32, ptr %[[X]], align 4
//      RAWLLVM:    %[[CMP:[0-9]+]] = icmp ne i32 %[[LOAD]], 0
//      RAWLLVM:    br i1 %[[CMP]], label %[[BODY:[^,]+]], label %[[EXIT:[^,]+]]
//      RAWLLVM:  [[BODY]]:
//      RAWLLVM:    call void @_Z8blackboxRKi(ptr noundef nonnull align 4 dereferenceable(4) %[[X]])
//      RAWLLVM:    br label %[[BACKEDGE:[^,]+]]
//      RAWLLVM:  [[BACKEDGE]]:
// RAWLLVM-NEXT:    call void @llvm.lifetime.end.p0(ptr %[[X]])
// RAWLLVM-NEXT:    br label %[[COND]]
//      RAWLLVM:  [[EXIT]]:
//      RAWLLVM:    call void @llvm.lifetime.end.p0(ptr %[[X]])
//      RAWLLVM:    ret void

// RAWLLVM-LABEL: @_Z36local_const_in_while_condition_breakv()
//      RAWLLVM:    %[[X_BREAK:[0-9]+]] = alloca i32, i64 1, align 4
//      RAWLLVM:  [[COND_BREAK:[^:]+]]:
// RAWLLVM-NEXT:    call void @llvm.lifetime.start.p0(ptr %[[X_BREAK]])
//      RAWLLVM:    br i1 %{{.*}}, label %[[BODY_BREAK:[^,]+]], label %[[EXIT_BREAK:[^,]+]]
//      RAWLLVM:  [[BODY_BREAK]]:
// RAWLLVM-NEXT:    br label %[[BREAK_JOIN:[^,]+]]
//      RAWLLVM:  [[BREAK_JOIN]]:
// RAWLLVM-NEXT:    br label %[[EXIT_BREAK]]
//      RAWLLVM:  [[EXIT_BREAK]]:
// RAWLLVM-NEXT:    call void @llvm.lifetime.end.p0(ptr %[[X_BREAK]])
// RAWLLVM-NEXT:    ret void

// RAWLLVM-LABEL: @_Z28local_const_in_for_conditionv()
//      RAWLLVM:    %[[XF:[0-9]+]] = alloca i32, i64 1, align 4
//      RAWLLVM:    br label %[[FOR_COND:[^,]+]]
//      RAWLLVM:  [[FOR_COND]]:
// RAWLLVM-NEXT:    call void @llvm.lifetime.start.p0(ptr %[[XF]])
//      RAWLLVM:    %[[FOR_INIT:[0-9]+]] = call noundef i32 @_Z11produce_intv()
//      RAWLLVM:    store i32 %[[FOR_INIT]], ptr %[[XF]], align 4
//      RAWLLVM:    %[[FOR_LOAD:[0-9]+]] = load i32, ptr %[[XF]], align 4
//      RAWLLVM:    %[[FOR_CMP:[0-9]+]] = icmp ne i32 %[[FOR_LOAD]], 0
//      RAWLLVM:    br i1 %[[FOR_CMP]], label %[[FOR_BODY:[^,]+]], label %[[FOR_EXIT:[^,]+]]
//      RAWLLVM:  [[FOR_BODY]]:
//      RAWLLVM:    call void @_Z8blackboxRKi(ptr noundef nonnull align 4 dereferenceable(4) %[[XF]])
//      RAWLLVM:    br label %[[FOR_CONT:[^,]+]]
//      RAWLLVM:  [[FOR_CONT]]:
//      RAWLLVM:    br label %[[FOR_STEP:[^,]+]]
//      RAWLLVM:  [[FOR_STEP]]:
// RAWLLVM-NEXT:    call void @llvm.lifetime.end.p0(ptr %[[XF]])
// RAWLLVM-NEXT:    br label %[[FOR_COND]]
//      RAWLLVM:  [[FOR_EXIT]]:
//      RAWLLVM:    call void @llvm.lifetime.end.p0(ptr %[[XF]])
//      RAWLLVM:    ret void

// RAWLLVM-LABEL: @_Z34local_const_in_for_condition_breakv()
//      RAWLLVM:    %[[XF_BREAK:[0-9]+]] = alloca i32, i64 1, align 4
//      RAWLLVM:  [[FOR_BREAK_COND:[^:]+]]:
// RAWLLVM-NEXT:    call void @llvm.lifetime.start.p0(ptr %[[XF_BREAK]])
//      RAWLLVM:    br i1 %{{.*}}, label %[[FOR_BREAK_BODY:[^,]+]], label %[[FOR_BREAK_EXIT:[^,]+]]
//      RAWLLVM:  [[FOR_BREAK_BODY]]:
// RAWLLVM-NEXT:    br label %[[FOR_BREAK_JOIN:[^,]+]]
//      RAWLLVM:  [[FOR_BREAK_JOIN]]:
// RAWLLVM-NEXT:    br label %[[FOR_BREAK_EXIT]]
//      RAWLLVM:  [[FOR_BREAK_EXIT]]:
// RAWLLVM-NEXT:    call void @llvm.lifetime.end.p0(ptr %[[XF_BREAK]])
// RAWLLVM-NEXT:    ret void
