// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A _Complex local variable or register-class parameter must get a
// DW_ATE_complex_float base type named "complex", not an "<opaque>"
// placeholder. (_Complex float is passed in registers; _Complex double/long
// double parameters are ABI-split and handled separately.)

double sink;

void locals(void) {
  _Complex float lf = 1.0f + 2.0fi;
  _Complex double ld = 3.0 + 4.0i;
  _Complex long double lld = 5.0L + 6.0Li;
  sink = __real__ lf + __real__ ld + (double)__real__ lld;
}

void cfloat_param(_Complex float a) { sink = __real__ a; }

// CIR-NOT: !DIBasicType(name: "<opaque>"
// CIR-DAG: !DIBasicType(name: "complex", size: 64, encoding: DW_ATE_complex_float)
// CIR-DAG: !DIBasicType(name: "complex", size: 128, encoding: DW_ATE_complex_float)
// CIR-DAG: !DIBasicType(name: "complex", size: 256, encoding: DW_ATE_complex_float)
// CIR-DAG: !DILocalVariable(name: "lf",{{.*}} type: ![[CF:[0-9]+]]
// CIR-DAG: !DILocalVariable(name: "ld",{{.*}} type: ![[CD:[0-9]+]]
// CIR-DAG: !DILocalVariable(name: "lld",{{.*}} type: ![[CLD:[0-9]+]]

// OGCG-DAG: !DIBasicType(name: "complex", size: 64, encoding: DW_ATE_complex_float)
// OGCG-DAG: !DIBasicType(name: "complex", size: 128, encoding: DW_ATE_complex_float)
// OGCG-DAG: !DIBasicType(name: "complex", size: 256, encoding: DW_ATE_complex_float)
