// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: cir-translate %t.cir -cir-to-llvmir -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

struct A { ~A(); };
A &&a = dynamic_cast<A&&>(A{});

//      CHECK: cir.func no_inline internal private @__cxx_global_var_init() section(".text.startup"){{.*}}{
// CHECK-NEXT:   %[[APTR:[0-9]+]] = cir.get_global @a : !cir.ptr<!cir.ptr<!rec_A>>
// CHECK-NEXT:   %[[GRPTR:[0-9]+]] = cir.get_global @_ZGR1a_ : !cir.ptr<!rec_A>
// CHECK:        cir.call @__cxa_atexit(
// CHECK:        cir.store{{.*}} %[[GRPTR]], %[[APTR]] : !cir.ptr<!rec_A>, !cir.ptr<!cir.ptr<!rec_A>>
// CHECK-NEXT:   cir.return
// CHECK-NEXT: }
// CHECK: cir.func {{.*}} @_ZN1AD1Ev({{.*}}) special_member<#cir.cxx_dtor<!rec_A>>
// CHECK: cir.global "private" internal @_ZGR1a_ = #cir.zero : !rec_A {alignment = 1 : i64}
// CHECK: cir.func {{.*}} @_GLOBAL__sub_I_tempref.cpp() section(".text.startup"){{.*}}{
// CHECK-NEXT:   cir.call @__cxx_global_var_init() : () -> ()
// CHECK-NEXT:   cir.return
// CHECK-NEXT: }

// LLVM-DAG: @_ZGR1a_ = internal global %struct.A zeroinitializer{{.*}}
// LLVM-DAG: @a = global ptr null, align 8
// LLVM-DAG: @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @_GLOBAL__sub_I_tempref.cpp, ptr null }]

// LLVM: define internal void @__cxx_global_var_init() {{.*}}section ".text.startup"
// LLVM:   call {{.*}} @__cxa_atexit(ptr @_ZN1AD1Ev, ptr @_ZGR1a_, ptr @__dso_handle)
// LLVM:   store ptr @_ZGR1a_, ptr @a, align 8
// LLVM:   ret void

// LLVM: declare {{.*}} void @_ZN1AD1Ev(ptr {{[^)]*}})

// LLVM: define {{.*}}void @_GLOBAL__sub_I_tempref.cpp() {{.*}}section ".text.startup"
// LLVM:   call void @__cxx_global_var_init()
// LLVM:   ret void
