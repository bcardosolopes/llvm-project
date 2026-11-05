// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -x objective-c -fobjc-runtime=gnustep-1.9 %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -x objective-c -fobjc-runtime=gnustep-1.9 %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Verify that ObjC object pointer types are handled correctly in CIR
// type conversion. ObjCObjectPointer maps to !cir.ptr<!void> (opaque
// pointer), matching OGCG's opaque LLVM pointer representation.

@interface Foo @end
@interface Bar : Foo @end

// CIR: cir.func {{.*}}@test_obj_ptr(%arg0: !cir.ptr<!void>
// LLVM: define {{.*}}void @test_obj_ptr(ptr
void test_obj_ptr(Foo *f) {}

// CIR: cir.func {{.*}}@test_derived_ptr(%arg0: !cir.ptr<!void>
// LLVM: define {{.*}}void @test_derived_ptr(ptr
void test_derived_ptr(Bar *b) {}

// CIR: cir.func {{.*}}@test_id(%arg0: !cir.ptr<!void>
// LLVM: define {{.*}}void @test_id(ptr
void test_id(id obj) {}

// CIR: cir.func {{.*}}@test_class(%arg0: !cir.ptr<!void>
// LLVM: define {{.*}}void @test_class(ptr
void test_class(Class cls) {}

// CIR: cir.func {{.*}}@test_return()
// LLVM: define {{.*}}ptr @test_return()
Foo *test_return(void) { return (void *)0; }

// CIR: cir.func {{.*}}@test_multiple(%arg0: !cir.ptr<!void>
// CIR-SAME: %arg1: !cir.ptr<!void>
// CIR-SAME: %arg2: !s32i
// LLVM: define {{.*}}void @test_multiple(ptr {{.*}}, ptr {{.*}}, i32
void test_multiple(Foo *a, Bar *b, int x) {}
