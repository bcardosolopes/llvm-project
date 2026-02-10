// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-llvm -fno-clangir-call-conv-lowering %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

class Mother {
public:
 virtual void MotherFoo() {}
 void simple() { }
 virtual void MotherFoo2() {}
};

class Father {
public:
 virtual void FatherFoo() {}
};

class Child : public Mother, public Father {
public:
 void MotherFoo() override {}
};

int main() {
    Mother *b = new Mother();
    b->MotherFoo();
    b->simple();
    Child *c = new Child();
    c->MotherFoo();
    return 0;
}

// CIR-DAG: !rec_Father = !cir.record<class "Father" {!cir.vptr}
// CIR-DAG: !rec_Mother = !cir.record<class "Mother" {!cir.vptr}
// CIR-DAG: !rec_Child = !cir.record<class "Child" {!rec_Mother, !rec_Father}

// Vtable globals are external in upstream
// CIR-DAG: cir.global "private"  external @_ZTV6Mother : !rec_anon_struct
// CIR-DAG: cir.global "private"  external @_ZTV5Child : !rec_anon_struct{{.*}}
// CIR-DAG: cir.global "private"  external @_ZTV6Father : !rec_anon_struct{{.*}}

// CIR: cir.func {{.*}} @_ZN6MotherC2Ev(%arg0: !cir.ptr<!rec_Mother>
// CIR:   %{{[0-9]+}} = cir.vtable.address_point(@_ZTV6Mother, address_point = <index = 0, offset = 2>) : !cir.vptr
// CIR:   %{{[0-9]+}} = cir.vtable.get_vptr %{{[0-9]+}} : !cir.ptr<!rec_Mother> -> !cir.ptr<!cir.vptr>
// CIR:   cir.return
// CIR: }

// LLVM-DAG: define linkonce_odr void @_ZN6MotherC2Ev(ptr %0)
// LLVM-DAG:   store ptr getelementptr inbounds nuw (i8, ptr @_ZTV6Mother, i64 16), ptr %{{[0-9]+}}, align 8
// LLVM-DAG:   ret void
// LLVM-DAG: }

// CIR: cir.func {{.*}} @_ZN5ChildC2Ev(%arg0: !cir.ptr<!rec_Child>
// CIR:   %{{[0-9]+}} = cir.vtable.address_point(@_ZTV5Child, address_point = <index = 0, offset = 2>) : !cir.vptr
// CIR:   %{{[0-9]+}} = cir.vtable.get_vptr %1 : !cir.ptr<!rec_Child> -> !cir.ptr<!cir.vptr>
// CIR:   %{{[0-9]+}} = cir.vtable.address_point(@_ZTV5Child, address_point = <index = 1, offset = 2>) : !cir.vptr
// CIR:   %7 = cir.base_class_addr %1 : !cir.ptr<!rec_Child> nonnull [8] -> !cir.ptr<!rec_Father>
// CIR:   %8 = cir.vtable.get_vptr %7 : !cir.ptr<!rec_Father> -> !cir.ptr<!cir.vptr>
// CIR:   cir.return
// CIR: }

// LLVM-DAG: define linkonce_odr void @_ZN5ChildC2Ev(ptr %0)
// LLVM-DAG:  store ptr getelementptr inbounds nuw (i8, ptr @_ZTV5Child, i64 16), ptr %{{[0-9]+}}, align 8
// LLVM-DAG:  %{{[0-9]+}} = getelementptr i8, ptr {{.*}}, i32 8
// LLVM-DAG:  store ptr getelementptr inbounds nuw (i8, ptr @_ZTV5Child, i64 48), ptr %{{[0-9]+}}, align 8
// LLVM-DAG:  ret void
// }

// CIR: cir.func {{.*}} @main() -> !s32i extra({{.*}}) {

// CIR:   %{{[0-9]+}} = cir.vtable.get_virtual_fn_addr %{{[0-9]+}}[0] : !cir.vptr -> !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!rec_Mother>)>>>

// CIR:   %{{[0-9]+}} = cir.vtable.get_virtual_fn_addr %{{[0-9]+}}[0] : !cir.vptr -> !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!rec_Child>)>>>

// CIR: }

// In upstream, vtable and typeinfo globals are external
// LLVM-DAG: @_ZTV6Mother = external global { [4 x ptr] }
// LLVM-DAG: @_ZTV5Child = external global { [4 x ptr], [3 x ptr] }
// LLVM-DAG: @_ZTV6Father = external global { [3 x ptr] }
