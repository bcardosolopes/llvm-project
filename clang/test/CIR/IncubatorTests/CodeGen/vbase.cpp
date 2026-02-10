// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

struct A {
  int a;
};

struct B:  virtual A {
  int b;
};

void ppp() { B b; }


// Vtable definition for B
// CIR:  cir.global "private"  external @_ZTV1B : !rec_anon_struct {alignment = 8 : i64}

// CIR:  cir.func {{.*}} @_ZN1BC1Ev
// CIR:    %[[THIS:.*]] = cir.load %{{.*}} : !cir.ptr<!cir.ptr<!rec_B>>, !cir.ptr<!rec_B>
// CIR:    cir.base_class_addr %[[THIS]] : !cir.ptr<!rec_B> nonnull [12] -> !cir.ptr<!rec_A>
// CIR:    cir.vtable.address_point(@_ZTV1B, address_point = <index = 0, offset = 3>) : !cir.vptr
// CIR:    cir.vtable.get_vptr %[[THIS]] : !cir.ptr<!rec_B> -> !cir.ptr<!cir.vptr>

// LLVM: @_ZTV1B = external global { [3 x ptr] }

// LLVM: define linkonce_odr void @_ZN1BC1Ev(ptr %0)
// LLVM:   store ptr getelementptr inbounds nuw (i8, ptr @_ZTV1B, i64 24), ptr %{{.*}}, align 8
