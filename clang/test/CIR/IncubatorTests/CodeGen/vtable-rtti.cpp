// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -mconstructor-aliases -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -fno-rtti -mconstructor-aliases -emit-cir %s -o %t2.cir
// RUN: FileCheck --input-file=%t2.cir --check-prefix=RTTI_DISABLED %s

class A
{
public:
    A() noexcept {}
    A(const A&) noexcept = default;

    virtual ~A() noexcept;
    virtual const char* quack() const noexcept;
};

class B : public A
{
public:
    virtual ~B() noexcept {}
};

// Vtable globals are external in upstream CIR
// CHECK-DAG: cir.global "private"  external @_ZTV1B : !rec_anon_struct
// CHECK-DAG: cir.global "private"  external @_ZTV1A : !rec_anon_struct
// RTTI_DISABLED-DAG: cir.global "private"  external @_ZTV1B : !rec_anon_struct
// RTTI_DISABLED-DAG: cir.global "private"  external @_ZTV1A : !rec_anon_struct

// B ctor => @B::B()
// Calls @A::A() and initialize __vptr with address of B's vtable.
//
// CHECK: cir.func {{.*}} @_ZN1BC2Ev(%arg0: !cir.ptr<!rec_B>
// RTTI_DISABLED: cir.func {{.*}} @_ZN1BC2Ev(%arg0: !cir.ptr<!rec_B>

// CHECK:   cir.base_class_addr %{{.*}} : !cir.ptr<!rec_B> nonnull [0] -> !cir.ptr<!rec_A>
// CHECK:   cir.call @_ZN1AC2Ev(%{{.*}}) {{.*}} : (!cir.ptr<!rec_A>) -> ()
// CHECK:   cir.vtable.address_point(@_ZTV1B, address_point = <index = 0, offset = 2>) : !cir.vptr
// CHECK:   cir.vtable.get_vptr %{{.*}} : !cir.ptr<!rec_B> -> !cir.ptr<!cir.vptr>
// CHECK:   cir.return
// CHECK: }

// foo - zero initialize object B and call ctor (@B::B())
//
// CHECK: cir.func {{.*}} @_Z3foov()
// CHECK:   cir.scope {
// CHECK:     %0 = cir.alloca !rec_B, !cir.ptr<!rec_B>, ["agg.tmp.ensured"] {alignment = 8 : i64}
// CHECK:     cir.const #cir.zero : !rec_B
// CHECK:     cir.call @_ZN1BC2Ev(%0) {{.*}} : (!cir.ptr<!rec_B>) -> ()
// CHECK:   }
// CHECK:   cir.return
// CHECK: }

// A ctor => @A::A()
// Calls @A::A() and initialize __vptr with address of A's vtable
//
// CHECK:  cir.func {{.*}} @_ZN1AC2Ev(%arg0: !cir.ptr<!rec_A>
// CHECK:    cir.vtable.address_point(@_ZTV1A, address_point = <index = 0, offset = 2>) : !cir.vptr
// CHECK:    cir.vtable.get_vptr %{{.*}} : !cir.ptr<!rec_A> -> !cir.ptr<!cir.vptr>
// CHECK:    cir.return
// CHECK:  }

// In upstream, vtable/typeinfo/RTTI globals are not emitted at CIR level.
// RTTI_DISABLED-NOT: cir.global{{.*}}@_ZTVN10__cxxabiv120__si_class_type_infoE
// RTTI_DISABLED-NOT: cir.global{{.*}}@_ZTS1B
// RTTI_DISABLED-NOT: cir.global{{.*}}@_ZTI1A
// RTTI_DISABLED-NOT: cir.global{{.*}}@_ZTI1B

// Checks for dtors in dtors.cpp

void foo() { B(); }
