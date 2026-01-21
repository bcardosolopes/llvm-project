// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -x c++ -fclangir -emit-cir -mmlir --mlir-print-ir-before=cir-canonicalize -o %t.cir %s 2>&1 | FileCheck --check-prefix=CIR-BEFORE %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -x c++ -fclangir -emit-cir -mmlir --mlir-print-ir-after=cir-canonicalize -o %t.cir %s 2>&1 | FileCheck --check-prefix=CIR-AFTER %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll --check-prefixes=LLVM %s

struct CX {
  double real;
  double imag;
};

void complex_lvalue_bitcast() {
  struct CX a;
  (double _Complex &)a = {};
}

// CIR-BEFORE: %{{.*}} = cir.cast bitcast %{{.*}} : !cir.ptr<!rec_CX> -> !cir.ptr<!cir.complex<!cir.double>>

// CIR-AFTER: %{{.*}} = cir.cast bitcast %{{.*}} : !cir.ptr<!rec_CX> -> !cir.ptr<!cir.complex<!cir.double>>

// LLVM: %[[A_ADDR:.*]] = alloca %struct.CX, i64 1, align 8
// LLVM: store { double, double } zeroinitializer, ptr %[[A_ADDR]], align 8

void complex_user_defined_cast() {
  struct Point {
    int x;
    int y;
    operator int _Complex() const { return {x, y}; }
  };

  Point p{1, 2};
  int _Complex c = p;
}

// CIR-AFTER: %[[P_ADDR:.*]] = cir.alloca !rec_Point, !cir.ptr<!rec_Point>, ["p", init]
// CIR-AFTER: %[[C_ADDR:.*]] = cir.alloca !cir.complex<!s32i>, !cir.ptr<!cir.complex<!s32i>>, ["c", init]
// CIR-AFTER: %[[P_GLOBAL:.*]] = cir.get_global @__const._Z25complex_user_defined_castv.p : !cir.ptr<!rec_Point>
// CIR-AFTER: cir.copy %[[P_GLOBAL]] to %[[P_ADDR]] : !cir.ptr<!rec_Point>
// CIR-AFTER: %[[POINT_TO_COMPLEX:.*]] = cir.call @_ZZ25complex_user_defined_castvENK5PointcvCiEv(%[[P_ADDR]]) : (!cir.ptr<!rec_Point>) -> !cir.complex<!s32i>
// CIR-AFTER: cir.store{{.*}} %[[POINT_TO_COMPLEX]], %[[C_ADDR]] : !cir.complex<!s32i>, !cir.ptr<!cir.complex<!s32i>>

// LLVM: %[[P_ADDR:.*]] = alloca %struct.Point, i64 1, align 4
// LLVM: %[[C_ADDR:.*]] = alloca { i32, i32 }, i64 1, align 4
// LLVM: %[[TMP:.*]] = alloca i64, i64 1, align 8
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr %[[P_ADDR]], ptr @__const._Z25complex_user_defined_castv.p, i64 8, i1 false)
// LLVM: %[[COERCED:.*]] = call i64 @_ZZ25complex_user_defined_castvENK5PointcvCiEv(ptr %[[P_ADDR]])
// LLVM: store i64 %[[COERCED]], ptr %[[TMP]], align 8
// LLVM: %[[COMPLEX:.*]] = load { i32, i32 }, ptr %[[TMP]], align 4
// LLVM: store { i32, i32 } %[[COMPLEX]], ptr %[[C_ADDR]], align 4
