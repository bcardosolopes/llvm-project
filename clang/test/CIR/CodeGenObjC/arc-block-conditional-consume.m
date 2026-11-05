// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-mlir=cir-flat -fblocks -fobjc-arc -fobjc-runtime-has-weak -fclangir -o - %s | FileCheck %s --check-prefix=CIR

id test22(int c, id x) {
  extern id test22_0(void);
  extern void test22_1(void);
  return c ? test22_0() : ({ id (^b)(void) = ^{ return x; }; test22_1(); b(); });
}

// CIR-LABEL: cir.func{{.*}} @test22
// CIR-DAG: %[[COND0:.*]] = cir.alloca !cir.ptr<!void>, !cir.ptr<!cir.ptr<!void>>, ["arc.consume"]
// CIR-DAG: %[[COND1:.*]] = cir.alloca !cir.ptr<!void>, !cir.ptr<!cir.ptr<!void>>, ["arc.consume"]
// CIR-DAG: %[[RELOAD0:.*]] = cir.load align(8) %[[COND0]]
// CIR-DAG: %[[RELOAD1:.*]] = cir.load align(8) %[[COND1]]
// CIR-DAG: cir.call @objc_release(%[[RELOAD0]]) nothrow {cir.imprecise_release}
// CIR-DAG: cir.call @objc_release(%[[RELOAD1]]) nothrow {cir.imprecise_release}
