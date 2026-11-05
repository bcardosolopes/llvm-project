// RUN: %clang_cc1 -triple=x86_64-linux-gnu -fclangir -emit-llvm -fxray-instrument -fxray-instruction-threshold=1 %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple=x86_64-linux-gnu -fclangir -emit-cir -fxray-instrument -fxray-instruction-threshold=1 %s -o %t.cir.mlir
// RUN: FileCheck --input-file=%t.cir.mlir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple=x86_64-linux-gnu -emit-llvm -fxray-instrument -fxray-instruction-threshold=1 %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

struct A {
  A();
  ~A();
};

A a;

// Both __cxx_global_var_init and _GLOBAL__sub_I_ should get
// xray-instruction-threshold from the default internal func attrs.
// CIR: #cir<extra({{.*}}"xray-instruction-threshold" = "1"{{.*}})>

// LLVM: define internal void @__cxx_global_var_init() [[INIT_ATTRS:#[0-9]+]]
// LLVM: define internal void @_GLOBAL__sub_I_xray_global_init.cpp() [[INIT_ATTRS]]
// LLVM: attributes [[INIT_ATTRS]] = { {{.*}}"xray-instruction-threshold"="1"{{.*}} }

// OGCG: define internal void @__cxx_global_var_init() [[INIT_ATTRS:#[0-9]+]]
// OGCG: define internal void @_GLOBAL__sub_I_xray_global_init.cpp() [[INIT_ATTRS]]
// OGCG: attributes [[INIT_ATTRS]] = { {{.*}}"xray-instruction-threshold"="1"{{.*}} }
