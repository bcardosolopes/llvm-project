// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat -fclangir-call-conv-lowering -mmlir --mlir-print-ir-after=cir-call-conv-lowering %s -o %t.cir 2>&1
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Test that multi-register struct returns are correctly decomposed into
// element-by-element stores. Previously crashed in createCoercedStore
// (LowerFunction.cpp) when the source type was a RecordType.
//
// On x86_64 SysV ABI, structs that fit in two eightbytes (16 bytes) are
// returned in two registers. The coerced return type is a struct like
// {i64, double} which must be decomposed into scalar stores.

// --- {int, double}: returned in RAX + XMM0 ---
struct IntDouble { int a; double b; };
IntDouble make_int_double();
int use_int_double() {
  IntDouble p = make_int_double();
  return p.a;
}

// CIR-LABEL: @_Z14use_int_doublev
// CIR:         cir.call @_Z15make_int_doublev() : () -> !rec_anon_struct
// CIR:         cir.get_member %{{.+}}[0] {name = ""} : !cir.ptr<!rec_anon_struct>
// CIR:         cir.extract_member %{{.+}}[0] : !rec_anon_struct
// CIR:         cir.store
// CIR:         cir.get_member %{{.+}}[1] {name = ""} : !cir.ptr<!rec_anon_struct>
// CIR:         cir.extract_member %{{.+}}[1] : !rec_anon_struct
// CIR:         cir.store

// LLVM-LABEL: @_Z14use_int_doublev
// LLVM:         call { i32, double } @_Z15make_int_doublev()
// LLVM:         getelementptr inbounds nuw { i32, double }
// LLVM:         extractvalue { i32, double } %{{.+}}, 0
// LLVM:         store i32
// LLVM:         getelementptr inbounds nuw { i32, double }
// LLVM:         extractvalue { i32, double } %{{.+}}, 1
// LLVM:         store double

// OGCG-LABEL: @_Z14use_int_doublev
// OGCG:         call { i32, double } @_Z15make_int_doublev()
// OGCG:         getelementptr {{.*}} { i32, double }
// OGCG:         extractvalue { i32, double } %{{.+}}, 0
// OGCG:         store i32
// OGCG:         getelementptr {{.*}} { i32, double }
// OGCG:         extractvalue { i32, double } %{{.+}}, 1
// OGCG:         store double

// --- {long, long}: returned in RAX + RDX ---
struct TwoLongs { long x; long y; };
TwoLongs make_two_longs();
long use_two_longs() {
  TwoLongs p = make_two_longs();
  return p.x + p.y;
}

// LLVM-LABEL: @_Z13use_two_longsv
// LLVM:         call { i64, i64 } @_Z14make_two_longsv()
// LLVM:         extractvalue { i64, i64 } %{{.+}}, 0
// LLVM:         extractvalue { i64, i64 } %{{.+}}, 1

// OGCG-LABEL: @_Z13use_two_longsv
// OGCG:         call { i64, i64 } @_Z14make_two_longsv()
// OGCG:         extractvalue { i64, i64 } %{{.+}}, 0
// OGCG:         extractvalue { i64, i64 } %{{.+}}, 1
