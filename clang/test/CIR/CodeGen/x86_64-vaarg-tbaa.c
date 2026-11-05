// RUN: %clang_cc1 -O -disable-llvm-passes -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR --check-prefix=COMMON --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -O -disable-llvm-passes -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --check-prefix=COMMON --input-file=%t.ogcg.ll %s

typedef __builtin_va_list va_list;

int test_int(va_list ap) {
  return __builtin_va_arg(ap, int);
}

// CIR-LABEL: define {{.*}} i32 @test_int(
// CIR: %[[INT_ADDR:.+]] = phi ptr [ %{{.*}}, %{{.*}} ], [ %{{.*}}, %{{.*}} ]
// CIR-NEXT: %[[INT_VAL:.+]] = load i32, ptr %[[INT_ADDR]], align 4, !tbaa ![[INT_TBAA:[0-9]+]]
//
// OGCG-LABEL: define {{.*}} i32 @test_int(
// OGCG: vaarg.end:
// OGCG-NEXT: %[[INT_ADDR:.+]] = phi ptr [ %{{.*}}, %vaarg.in_reg ], [ %{{.*}}, %vaarg.in_mem ]
// OGCG-NEXT: %[[INT_VAL:.+]] = load i32, ptr %[[INT_ADDR]], align 4, !tbaa ![[INT_TBAA:[0-9]+]]
// OGCG-NEXT: ret i32 %[[INT_VAL]]

double test_double(va_list ap) {
  return __builtin_va_arg(ap, double);
}

// CIR-LABEL: define {{.*}} double @test_double(
// CIR: %[[DOUBLE_ADDR:.+]] = phi ptr [ %{{.*}}, %{{.*}} ], [ %{{.*}}, %{{.*}} ]
// CIR-NEXT: %[[DOUBLE_VAL:.+]] = load double, ptr %[[DOUBLE_ADDR]], align 8, !tbaa ![[DOUBLE_TBAA:[0-9]+]]
//
// OGCG-LABEL: define {{.*}} double @test_double(
// OGCG: vaarg.end:
// OGCG-NEXT: %[[DOUBLE_ADDR:.+]] = phi ptr [ %{{.*}}, %vaarg.in_reg ], [ %{{.*}}, %vaarg.in_mem ]
// OGCG-NEXT: %[[DOUBLE_VAL:.+]] = load double, ptr %[[DOUBLE_ADDR]], align 8, !tbaa ![[DOUBLE_TBAA:[0-9]+]]
// OGCG-NEXT: ret double %[[DOUBLE_VAL]]

long double test_longdouble(va_list ap) {
  return __builtin_va_arg(ap, long double);
}

// CIR-LABEL: define {{.*}} x86_fp80 @test_longdouble(
// CIR: %[[LONGDOUBLE_VAL:.+]] = load x86_fp80, ptr %{{.*}}, align 16, !tbaa ![[LONGDOUBLE_TBAA:[0-9]+]]
// CIR-NEXT: ret x86_fp80 %[[LONGDOUBLE_VAL]]
//
// OGCG-LABEL: define {{.*}} x86_fp80 @test_longdouble(
// OGCG: %[[LONGDOUBLE_VAL:.+]] = load x86_fp80, ptr %{{.*}}, align 16, !tbaa ![[LONGDOUBLE_TBAA:[0-9]+]]
// OGCG-NEXT: ret x86_fp80 %[[LONGDOUBLE_VAL]]
//
// COMMON: ![[INT_TBAA]] = !{![[INT_NODE:[0-9]+]], ![[INT_NODE]], i64 0}
// COMMON: ![[INT_NODE]] = !{!"int", ![[CHAR_NODE:[0-9]+]], i64 0}
// COMMON: ![[DOUBLE_TBAA]] = !{![[DOUBLE_NODE:[0-9]+]], ![[DOUBLE_NODE]], i64 0}
// COMMON: ![[DOUBLE_NODE]] = !{!"double", ![[CHAR_NODE]], i64 0}
// COMMON: ![[LONGDOUBLE_TBAA]] = !{![[LONGDOUBLE_NODE:[0-9]+]], ![[LONGDOUBLE_NODE]], i64 0}
// COMMON: ![[LONGDOUBLE_NODE]] = !{!"long double", ![[CHAR_NODE]], i64 0}
