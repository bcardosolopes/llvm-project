// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

struct A {
  int a[5];
  A() : a{1, 2, 3, 4, 5} {}
};

void use(const A &);
void f() {
  A x;
  use(x);
}

// CIR: cir.global "private"{{.*}}@".compoundLiteral.
// CIR-SAME: #cir.const_array<[#cir.int<1> : !s32i, #cir.int<2> : !s32i, #cir.int<3> : !s32i, #cir.int<4> : !s32i, #cir.int<5> : !s32i]> : !cir.array<!s32i x 5>
// CIR-LABEL: cir.func{{.*}} @_ZN1AC2Ev
// CIR: %[[ARR:.*]] = cir.get_member %{{.*}}[0] {name = "a"} : !cir.ptr<!rec_A> -> !cir.ptr<!cir.array<!s32i x 5>>
// CIR: %[[CONST:.*]] = cir.get_global @".compoundLiteral.
// CIR: cir.copy %[[CONST]] to %[[ARR]] {dst_alignment = 4 : i64, src_alignment = 4 : i64} : !cir.ptr<!cir.array<!s32i x 5>>
// CIR-NOT: cir.ptr_stride

// LLVM-LABEL: define linkonce_odr void @_ZN1AC2Ev
// LLVM: %[[ARR:.*]] = getelementptr inbounds nuw %struct.A, ptr %{{.*}}, i32 0, i32 0
// LLVM: call void @llvm.memcpy{{.*}}(ptr align 4 %[[ARR]], ptr align 4 @.compoundLiteral.
