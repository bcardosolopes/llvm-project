// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck -allow-deprecated-dag-overlap --check-prefix=CIR --input-file=%t.cir %s
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

// CIR-DAG: ![[VTypeInfoA:rec_.*]] = !cir.record<struct  {!cir.ptr<!u8i>, !cir.ptr<!u8i>}>
// CIR-DAG: ![[VTypeInfoB:rec_.*]] = !cir.record<struct  {!cir.ptr<!u8i>, !cir.ptr<!u8i>, !u32i, !u32i, !cir.ptr<!u8i>, !s64i, !cir.ptr<!u8i>, !s64i}>
// CIR-DAG: ![[VPtrTypeMother:rec_.*]] = !cir.record<struct  {!cir.array<!cir.ptr<!u8i> x 4>}>
// CIR-DAG: ![[VPtrTypeFather:rec_.*]] = !cir.record<struct  {!cir.array<!cir.ptr<!u8i> x 3>}>
// CIR-DAG: ![[VPtrTypeChild:rec_.*]] = !cir.record<struct  {!cir.array<!cir.ptr<!u8i> x 4>, !cir.array<!cir.ptr<!u8i> x 3>}>
// CIR-DAG: !rec_Father = !cir.record<class "Father" {!cir.vptr}
// CIR-DAG: !rec_Mother = !cir.record<class "Mother" {!cir.vptr}
// CIR-DAG: !rec_Child = !cir.record<class "Child" {!rec_Mother, !rec_Father}

//   vtable for Mother
// CIR-DAG: cir.global{{.*}}linkonce_odr{{.*}}@_ZTV6Mother = #cir.vtable<{#cir.const_array<[#cir.ptr<null> : !cir.ptr<!u8i>, #cir.global_view<@_ZTI6Mother> : !cir.ptr<!u8i>, #cir.global_view<@_ZN6Mother9MotherFooEv> : !cir.ptr<!u8i>, #cir.global_view<@_ZN6Mother10MotherFoo2Ev> : !cir.ptr<!u8i>]> : !cir.array<!cir.ptr<!u8i> x 4>}> : ![[VPtrTypeMother]] {alignment = 8 : i64}

//   vtable for __cxxabiv1::__class_type_info
// CIR-DAG: cir.global{{.*}}external{{.*}}@_ZTVN10__cxxabiv117__class_type_infoE : !cir.ptr<!cir.ptr<!u8i>>

//   typeinfo name for Mother
// CIR-DAG: cir.global{{.*}}linkonce_odr comdat{{.*}}@_ZTS6Mother = #cir.const_array<"6Mother" : !cir.array<!s8i x 7>> : !cir.array<!s8i x 7> {alignment = 1 : i64}

//   typeinfo for Mother
// CIR-DAG: cir.global{{.*}}constant external{{.*}}@_ZTI6Mother = #cir.typeinfo<{#cir.global_view<@_ZTVN10__cxxabiv117__class_type_infoE, [2 : i32]> : !cir.ptr<!u8i>, #cir.global_view<@_ZTS6Mother> : !cir.ptr<!u8i>}> : ![[VTypeInfoA]] {alignment = 8 : i64}

//   vtable for Child
// CIR-DAG: cir.global{{.*}}linkonce_odr{{.*}}@_ZTV5Child = #cir.vtable<{#cir.const_array<[#cir.ptr<null> : !cir.ptr<!u8i>, #cir.global_view<@_ZTI5Child> : !cir.ptr<!u8i>, #cir.global_view<@_ZN5Child9MotherFooEv> : !cir.ptr<!u8i>, #cir.global_view<@_ZN6Mother10MotherFoo2Ev> : !cir.ptr<!u8i>]> : !cir.array<!cir.ptr<!u8i> x 4>, #cir.const_array<[#cir.ptr<-8 : i64> : !cir.ptr<!u8i>, #cir.global_view<@_ZTI5Child> : !cir.ptr<!u8i>, #cir.global_view<@_ZN6Father9FatherFooEv> : !cir.ptr<!u8i>]> : !cir.array<!cir.ptr<!u8i> x 3>}> : ![[VPtrTypeChild]] {alignment = 8 : i64}

//   vtable for __cxxabiv1::__vmi_class_type_info
// CIR-DAG: cir.global{{.*}}external{{.*}}@_ZTVN10__cxxabiv121__vmi_class_type_infoE : !cir.ptr<!cir.ptr<!u8i>>

//   typeinfo name for Child
// CIR-DAG: cir.global{{.*}}linkonce_odr comdat{{.*}}@_ZTS5Child = #cir.const_array<"5Child" : !cir.array<!s8i x 6>> : !cir.array<!s8i x 6> {alignment = 1 : i64}

//   typeinfo name for Father
// CIR-DAG: cir.global{{.*}}linkonce_odr comdat{{.*}}@_ZTS6Father = #cir.const_array<"6Father" : !cir.array<!s8i x 7>> : !cir.array<!s8i x 7> {alignment = 1 : i64}

//   typeinfo for Father
// CIR-DAG: cir.global{{.*}}constant external{{.*}}@_ZTI6Father = #cir.typeinfo<{#cir.global_view<@_ZTVN10__cxxabiv117__class_type_infoE, [2 : i32]> : !cir.ptr<!u8i>, #cir.global_view<@_ZTS6Father> : !cir.ptr<!u8i>}> : ![[VTypeInfoA]] {alignment = 8 : i64}

//   typeinfo for Child
// CIR-DAG: cir.global{{.*}}constant external{{.*}}@_ZTI5Child = #cir.typeinfo<{#cir.global_view<@_ZTVN10__cxxabiv121__vmi_class_type_infoE, [2 : i32]> : !cir.ptr<!u8i>, #cir.global_view<@_ZTS5Child> : !cir.ptr<!u8i>, #cir.int<0> : !u32i, #cir.int<2> : !u32i, #cir.global_view<@_ZTI6Mother> : !cir.ptr<!u8i>, #cir.int<2> : !s64i, #cir.global_view<@_ZTI6Father> : !cir.ptr<!u8i>, #cir.int<2050> : !s64i}> : ![[VTypeInfoB]] {alignment = 8 : i64}

//   vtable for Father
// CIR-DAG: cir.global{{.*}}linkonce_odr{{.*}}@_ZTV6Father = #cir.vtable<{#cir.const_array<[#cir.ptr<null> : !cir.ptr<!u8i>, #cir.global_view<@_ZTI6Father> : !cir.ptr<!u8i>, #cir.global_view<@_ZN6Father9FatherFooEv> : !cir.ptr<!u8i>]> : !cir.array<!cir.ptr<!u8i> x 3>}> : ![[VPtrTypeFather]] {alignment = 8 : i64}

// CIR: cir.func {{.*}} @_ZN6MotherC2Ev(%arg0: !cir.ptr<!rec_Mother>
// CIR:   %{{[0-9]+}} = cir.vtable.address_point(@_ZTV6Mother, address_point = <index = 0, offset = 2>) : !cir.vptr
// CIR:   %{{[0-9]+}} = cir.vtable.get_vptr %{{[0-9]+}} : !cir.ptr<!rec_Mother> -> !cir.ptr<!cir.vptr>
// CIR:   cir.store{{.*}} %{{[0-9]+}}, %{{[0-9]+}} : !cir.vptr, !cir.ptr<!cir.vptr>
// CIR:   cir.return
// CIR: }

// LLVM-DAG: define linkonce_odr void @_ZN6MotherC2Ev(ptr %0)
// LLVM-DAG:   store ptr getelementptr inbounds nuw (i8, ptr @_ZTV6Mother, i64 16), ptr %{{[0-9]+}}, align 8
// LLVM-DAG:   ret void
// LLVM-DAG: }

// CIR: cir.func {{.*}} @_ZN5ChildC2Ev(%arg0: !cir.ptr<!rec_Child>
// CIR:   %{{[0-9]+}} = cir.vtable.address_point(@_ZTV5Child, address_point = <index = 0, offset = 2>) : !cir.vptr
// CIR:   %{{[0-9]+}} = cir.vtable.get_vptr %{{[0-9]+}} : !cir.ptr<!rec_Child> -> !cir.ptr<!cir.vptr>
// CIR:   cir.store{{.*}} %{{[0-9]+}}, %{{[0-9]+}} : !cir.vptr, !cir.ptr<!cir.vptr>
// CIR:   %{{[0-9]+}} = cir.vtable.address_point(@_ZTV5Child, address_point = <index = 1, offset = 2>) : !cir.vptr
// CIR:   cir.base_class_addr %{{[0-9]+}} : !cir.ptr<!rec_Child> nonnull [8] -> !cir.ptr<!rec_Father>
// CIR:   cir.vtable.get_vptr %{{[0-9]+}} : !cir.ptr<!rec_Father> -> !cir.ptr<!cir.vptr>
// CIR:   cir.store{{.*}} %{{[0-9]+}}, %{{[0-9]+}} : !cir.vptr, !cir.ptr<!cir.vptr>
// CIR:   cir.return
// CIR: }

// LLVM-DAG: define linkonce_odr void @_ZN5ChildC2Ev(ptr %0)
// LLVM-DAG:  store ptr getelementptr inbounds nuw (i8, ptr @_ZTV5Child, i64 16), ptr %{{[0-9]+}}, align 8
// LLVM-DAG:  %{{[0-9]+}} = getelementptr i8, ptr {{.*}}, i32 8
// LLVM-DAG:  store ptr getelementptr inbounds nuw (i8, ptr @_ZTV5Child, i64 48), ptr %{{[0-9]+}}, align 8
// LLVM-DAG:  ret void
// }

// CIR: cir.func {{.*}} @main() -> !s32i extra(#fn_attr) {

// CIR:   %{{[0-9]+}} = cir.vtable.get_virtual_fn_addr %{{[0-9]+}}[0] : !cir.vptr -> !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!rec_Mother>)>>>

// CIR:   %{{[0-9]+}} = cir.vtable.get_virtual_fn_addr %{{[0-9]+}}[0] : !cir.vptr -> !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!rec_Child>)>>>

// CIR: }

// Vtable globals in LLVM IR
// LLVM-DAG: @_ZTV6Mother = linkonce_odr global { [4 x ptr] } { [4 x ptr] [ptr null, ptr @_ZTI6Mother, ptr @_ZN6Mother9MotherFooEv, ptr @_ZN6Mother10MotherFoo2Ev] }
// LLVM-DAG: @_ZTV5Child = linkonce_odr global { [4 x ptr], [3 x ptr] } { [4 x ptr] [ptr null, ptr @_ZTI5Child, ptr @_ZN5Child9MotherFooEv, ptr @_ZN6Mother10MotherFoo2Ev], [3 x ptr] [ptr inttoptr (i64 -8 to ptr), ptr @_ZTI5Child, ptr @_ZN6Father9FatherFooEv] }
// LLVM-DAG: @_ZTV6Father = linkonce_odr global { [3 x ptr] } { [3 x ptr] [ptr null, ptr @_ZTI6Father, ptr @_ZN6Father9FatherFooEv] }

// LLVM-DAG: @_ZTVN10__cxxabiv117__class_type_infoE = external global ptr
// LLVM-DAG: @_ZTS6Mother = linkonce_odr global [7 x i8] c"6Mother", comdat
// LLVM-DAG: @_ZTI6Mother = constant { ptr, ptr } { ptr getelementptr (i8, ptr @_ZTVN10__cxxabiv117__class_type_infoE, i64 16), ptr @_ZTS6Mother }
// LLVM-DAG: @_ZTVN10__cxxabiv121__vmi_class_type_infoE = external global ptr
// LLVM-DAG: @_ZTS5Child = linkonce_odr global [6 x i8] c"5Child", comdat
// LLVM-DAG: @_ZTS6Father = linkonce_odr global [7 x i8] c"6Father", comdat
// LLVM-DAG: @_ZTI6Father = constant { ptr, ptr } { ptr getelementptr (i8, ptr @_ZTVN10__cxxabiv117__class_type_infoE, i64 16), ptr @_ZTS6Father }
// LLVM-DAG: @_ZTI5Child = constant { ptr, ptr, i32, i32, ptr, i64, ptr, i64 } { ptr getelementptr (i8, ptr @_ZTVN10__cxxabiv121__vmi_class_type_infoE, i64 16), ptr @_ZTS5Child, i32 0, i32 2, ptr @_ZTI6Mother, i64 2, ptr @_ZTI6Father, i64 2050 }
