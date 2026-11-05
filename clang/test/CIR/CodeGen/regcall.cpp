// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Test __regcall calling convention lowering.

void __attribute__((regcall)) f_scalar(int a, int b) {}

// CIR-LABEL: @_Z20__regcall3__f_scalarii
// CIR-SAME:  cc(x86_regcall)

// LLVM-LABEL: define dso_local x86_regcallcc void @_Z20__regcall3__f_scalarii
// LLVM-SAME: (i32 {{.*}}, i32 {{.*}})

// OGCG-LABEL: define dso_local x86_regcallcc void @_Z20__regcall3__f_scalarii
// OGCG-SAME: (i32 {{.*}}, i32 {{.*}})

void call_scalar() { f_scalar(1, 2); }

// CIR-LABEL: @_Z11call_scalarv
// CIR:   cir.call @_Z20__regcall3__f_scalarii({{.*}}) {{.*}}cc(x86_regcall)

// LLVM-LABEL: define dso_local void @_Z11call_scalarv
// LLVM:   call x86_regcallcc void @_Z20__regcall3__f_scalarii(i32 {{.*}}, i32 {{.*}})

// OGCG-LABEL: define dso_local void @_Z11call_scalarv
// OGCG:   call x86_regcallcc void @_Z20__regcall3__f_scalarii(i32 {{.*}}, i32 {{.*}})

struct S { int a; int b; };

void __attribute__((regcall)) f_struct(S s) {}

// LLVM-LABEL: define dso_local x86_regcallcc void @_Z20__regcall3__f_struct1S
// LLVM-SAME: (i32 %{{.*}}, i32 %{{.*}})

// OGCG-LABEL: define dso_local x86_regcallcc void @_Z20__regcall3__f_struct1S
// OGCG-SAME: (i32 %{{.*}}, i32 %{{.*}})

S __attribute__((regcall)) ret_struct() { return {1, 2}; }

// LLVM-LABEL: define dso_local x86_regcallcc %struct.S @_Z22__regcall3__ret_structv
// OGCG-LABEL: define dso_local x86_regcallcc %struct.S @_Z22__regcall3__ret_structv

// A union > 512 bits should be classified as MEMORY (passed indirectly)
// even under regcall, because the IsRegCall bypass only applies to
// struct types routed through classifyRegCallStructType.
union LargeUnion {
  long long a[9]; // 576 bits > 512 bit threshold
  double b[9];
};

void __attribute__((regcall)) f_large_union(LargeUnion u) {}

// LLVM-LABEL: define dso_local x86_regcallcc void @_Z25__regcall3__f_large_union10LargeUnion
// LLVM-SAME: (ptr {{.*}}byval(%union.LargeUnion){{.*}})

// OGCG-LABEL: define dso_local x86_regcallcc void @_Z25__regcall3__f_large_union10LargeUnion
// OGCG-SAME: (ptr {{.*}}byval(%union.LargeUnion){{.*}})
