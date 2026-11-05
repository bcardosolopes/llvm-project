// RUN: %clang_cc1 -triple x86_64-unknown-unknown -Wno-return-type-c-linkage -fclangir -emit-mlir=cir-abi -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -Wno-return-type-c-linkage -std=c++20 -O1 -disable-llvm-passes -fclangir -emit-mlir=cir-abi -o - %s | FileCheck %s --check-prefix=CIR-O1
// RUN: %clang_cc1 -triple i386-unknown-unknown -Wno-return-type-c-linkage -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM-I386
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -Wno-return-type-c-linkage -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM-X64
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -Wno-return-type-c-linkage -std=c++20 -O1 -disable-llvm-passes -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM-X64-O1

struct A {
  long x, y;
};

struct B {
  long x, y, z, w;
};

extern "C" {

int f1(A, A, A, A);
B f2(void);
_Complex float f3(void);
A &f4();

}

void test() {
  A a;

  __builtin_call_with_static_chain(f1(a, a, a, a), f1);
  __builtin_call_with_static_chain(f2(), f2);
  __builtin_call_with_static_chain(f3(), f3);
  __builtin_call_with_static_chain(f4(), f4);
}

// CIR-LABEL: cir.func {{.*}}@_Z4testv()
// CIR: cir.call @f1(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}) {cir.call_arg_attrs = [{cir.nest, cir.noundef}, {}, {}, {}, {}, {}, {}, {}]}
// CIR: cir.call @f2(%{{.*}}, %{{.*}}) {cir.call_arg_attrs = [{}, {cir.nest, cir.noundef}]}
// CIR: cir.call @f3(%{{.*}}) {cir.call_arg_attrs = [{cir.nest, cir.noundef}]}
// CIR: cir.call @f4(%{{.*}}) {cir.call_arg_attrs = [{cir.nest, cir.noundef}]}

// CIR-O1-LABEL: cir.func {{.*}}@_Z4testv()
// CIR-O1-DAG: %[[F2TMP:.*]] = cir.alloca !rec_B, !cir.ptr<!rec_B>, ["tmp"] {alignment = 8 : i64}
// CIR-O1: cir.lifetime.start %[[F2TMP]] : !cir.ptr<!rec_B>
// CIR-O1: cir.call @f2(%[[F2TMP]], %{{.*}}) {cir.call_arg_attrs = [{}, {cir.nest, cir.noundef}]}
// CIR-O1: cir.lifetime.end %[[F2TMP]] : !cir.ptr<!rec_B>

// LLVM-I386: call i32 @f1(ptr nest noundef @f1
// LLVM-I386: call void @f2(ptr dead_on_unwind writable sret(%struct.B) align 4 %{{[0-9a-z]+}}, ptr nest noundef @f2)
// LLVM-I386: call i64 @f3(ptr nest noundef @f3)
// LLVM-I386: call nonnull align 4 dereferenceable(8) ptr @f4(ptr nest noundef @f4)
// LLVM-I386: declare nonnull align 4 dereferenceable(8) ptr @f4()
// LLVM-I386: declare i64 @f3()
// LLVM-I386: declare void @f2(ptr dead_on_unwind writable sret(%struct.B) align 4)
// LLVM-I386: declare i32 @f1(i32, i32, i32, i32, i32, i32, i32, i32)

// LLVM-X64: call i32 @f1(ptr nest noundef @f1
// LLVM-X64: call void @f2(ptr dead_on_unwind writable sret(%struct.B) align 8 %{{[0-9a-z]+}}, ptr nest noundef @f2)
// LLVM-X64: call <2 x float> @f3(ptr nest noundef @f3)
// LLVM-X64: call nonnull align 8 dereferenceable(16) ptr @f4(ptr nest noundef @f4)

// LLVM-X64-O1-LABEL: define{{.*}} void @_Z4testv()
// LLVM-X64-O1: %[[F2TMP:.*]] = alloca %struct.B, i64 1, align 8
// LLVM-X64-O1: call void @llvm.lifetime.start.p0(ptr %[[F2TMP]])
// LLVM-X64-O1: call void @f2(ptr dead_on_unwind writable sret(%struct.B) align 8 %[[F2TMP]], ptr nest noundef @f2)
// LLVM-X64-O1: call void @llvm.lifetime.end.p0(ptr %[[F2TMP]])
