// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

class A {
    int a;
};

class B {
    int b;
public:
    A *getAsA();
};

class X : public A, public B {
    int x;
};

A *B::getAsA() {
  return static_cast<X*>(this);
}

// CIR-LABEL: @_ZN1B6getAsAEv
// CIR: %[[THIS_ADDR:.*]] = cir.alloca !cir.ptr<!rec_B>, !cir.ptr<!cir.ptr<!rec_B>>, ["this", init] {alignment = 8 : i64}
// CIR: cir.store %arg0, %[[THIS_ADDR]] : !cir.ptr<!rec_B>, !cir.ptr<!cir.ptr<!rec_B>>
// CIR: %[[THIS:.*]] = cir.load %[[THIS_ADDR]] : !cir.ptr<!cir.ptr<!rec_B>>, !cir.ptr<!rec_B>
// CIR: %[[X_PTR:.*]] = cir.derived_class_addr %[[THIS]] : !cir.ptr<!rec_B> nonnull [4] -> !cir.ptr<!rec_X>
// CIR: %[[A_PTR:.*]] = cir.base_class_addr %[[X_PTR]] : !cir.ptr<!rec_X> nonnull [0] -> !cir.ptr<!rec_A>
// CIR: cir.return %[[A_PTR]] : !cir.ptr<!rec_A>

// LLVM-LABEL: @_ZN1B6getAsAEv
// LLVM:  %[[VAL_1:.*]] = alloca ptr, i64 1, align 8
// LLVM:  store ptr %[[VAL_2:.*]], ptr %[[VAL_0:.*]], align 8
// LLVM:  %[[VAL_3:.*]] = load ptr, ptr %[[VAL_0]], align 8
// LLVM:  %[[VAL_4:.*]] = getelementptr inbounds i8, ptr %[[VAL_3]], i64 -4
// LLVM-NOT: select i1
// LLVM:  ret ptr
