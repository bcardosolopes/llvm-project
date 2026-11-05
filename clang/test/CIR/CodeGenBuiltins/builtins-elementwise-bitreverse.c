// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

typedef short si8 __attribute__((ext_vector_type(8)));

// Signed scalar bitreverse: should cast to unsigned, apply, cast back.
void test_bitreverse_signed_int(int si) {
  si = __builtin_elementwise_bitreverse(si);
}

// CIR-LABEL: @test_bitreverse_signed_int
// CIR:         %[[LOAD:.+]] = cir.load {{.*}} : !cir.ptr<!s32i>, !s32i
// CIR-NEXT:    %[[CAST:.+]] = cir.cast bitcast %[[LOAD]] : !s32i -> !u32i
// CIR-NEXT:    %[[REV:.+]] = cir.bitreverse %[[CAST]] : !u32i
// CIR-NEXT:    %[[BACK:.+]] = cir.cast bitcast %[[REV]] : !u32i -> !s32i

// LLVM-LABEL: @test_bitreverse_signed_int
// LLVM:         %[[V:.+]] = load i32, ptr %{{.+}}, align 4
// LLVM-NEXT:    %[[REV:.+]] = call i32 @llvm.bitreverse.i32(i32 %[[V]])

// OGCG-LABEL: @test_bitreverse_signed_int
// OGCG:         %[[V:.+]] = load i32, ptr %{{.+}}, align 4
// OGCG-NEXT:    %[[REV:.+]] = call i32 @llvm.bitreverse.i32(i32 %[[V]])

// Signed vector bitreverse: should bitcast vector to unsigned-element vector.
void test_bitreverse_signed_vec(si8 vi) {
  vi = __builtin_elementwise_bitreverse(vi);
}

// CIR-LABEL: @test_bitreverse_signed_vec
// CIR:         %[[LOAD:.+]] = cir.load {{.*}} : !cir.ptr<!cir.vector<8 x !s16i>>, !cir.vector<8 x !s16i>
// CIR-NEXT:    %[[CAST:.+]] = cir.cast bitcast %[[LOAD]] : !cir.vector<8 x !s16i> -> !cir.vector<8 x !u16i>
// CIR-NEXT:    %[[REV:.+]] = cir.bitreverse %[[CAST]] : !cir.vector<8 x !u16i>
// CIR-NEXT:    %[[BACK:.+]] = cir.cast bitcast %[[REV]] : !cir.vector<8 x !u16i> -> !cir.vector<8 x !s16i>

// LLVM-LABEL: @test_bitreverse_signed_vec
// LLVM:         %[[V:.+]] = load <8 x i16>, ptr %{{.+}}, align 16
// LLVM-NEXT:    %[[REV:.+]] = call <8 x i16> @llvm.bitreverse.v8i16(<8 x i16> %[[V]])

// OGCG-LABEL: @test_bitreverse_signed_vec
// OGCG:         %[[V:.+]] = load <8 x i16>, ptr %{{.+}}, align 16
// OGCG-NEXT:    %[[REV:.+]] = call <8 x i16> @llvm.bitreverse.v8i16(<8 x i16> %[[V]])

// Signed scalar popcount: should cast to unsigned, apply, cast back.
void test_popcount_signed_int(int si) {
  si = __builtin_elementwise_popcount(si);
}

// CIR-LABEL: @test_popcount_signed_int
// CIR:         %[[LOAD:.+]] = cir.load {{.*}} : !cir.ptr<!s32i>, !s32i
// CIR-NEXT:    %[[CAST:.+]] = cir.cast bitcast %[[LOAD]] : !s32i -> !u32i
// CIR-NEXT:    %[[POP:.+]] = cir.popcount %[[CAST]] : !u32i
// CIR-NEXT:    %[[BACK:.+]] = cir.cast bitcast %[[POP]] : !u32i -> !s32i

// LLVM-LABEL: @test_popcount_signed_int
// LLVM:         %[[V:.+]] = load i32, ptr %{{.+}}, align 4
// LLVM-NEXT:    %[[POP:.+]] = call i32 @llvm.ctpop.i32(i32 %[[V]])

// OGCG-LABEL: @test_popcount_signed_int
// OGCG:         %[[V:.+]] = load i32, ptr %{{.+}}, align 4
// OGCG-NEXT:    %[[POP:.+]] = call i32 @llvm.ctpop.i32(i32 %[[V]])
