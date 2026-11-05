// RUN: %clang_cc1 %s -triple amdgcn-amd-amdhsa -emit-llvm -fcxx-exceptions -fexceptions -std=c++11 -o - | FileCheck %s
// RUN: %clang_cc1 %s -triple spirv64-amd-amdhsa -emit-llvm -fcxx-exceptions -fexceptions -std=c++11 -o - | FileCheck %s --check-prefix=WITH-NONZERO-DEFAULT-AS
// RUN: %if cir-support %{ %clang_cc1 %s -triple amdgcn-amd-amdhsa -fclangir -emit-llvm -fcxx-exceptions -fexceptions -std=c++11 -o - | FileCheck %s --check-prefixes=CHECK,CIR %}
// RUN: %if cir-support %{ %clang_cc1 %s -triple spirv64-amd-amdhsa -fclangir -emit-llvm -fcxx-exceptions -fexceptions -std=c++11 -o - | FileCheck %s --check-prefixes=WITH-NONZERO-DEFAULT-AS,CIR-WITH-NONZERO-DEFAULT-AS %}

struct X {
  ~X();
};

struct Error {
  Error(const X&) noexcept;
};

void f() {
  try {
    throw Error(X());
  } catch (...) { }
}

// CHECK-DAG: declare void @__cxa_throw(ptr, ptr addrspace(1), ptr)
// WITH-NONZERO-DEFAULT-AS-DAG: declare{{.*}} void @__cxa_throw(ptr addrspace(4), ptr addrspace(1), ptr addrspace(4))
// CIR-DAG: declare ptr @__cxa_allocate_exception(i64)
// CIR-DAG: @_ZTI5Error = linkonce_odr addrspace(1) constant { ptr addrspace(1), ptr addrspace(1) }
// CIR-DAG: @_ZTVN10__cxxabiv117__class_type_infoE = external addrspace(1) global [0 x ptr addrspace(1)]
// CIR-WITH-NONZERO-DEFAULT-AS-DAG: declare i32 @__gxx_personality_v0(...) addrspace(4)
// CIR-WITH-NONZERO-DEFAULT-AS-DAG: declare{{.*}} ptr addrspace(4) @__cxa_allocate_exception(i64) addrspace(4)
// CIR-WITH-NONZERO-DEFAULT-AS-DAG: @_ZTI5Error = linkonce_odr addrspace(1) constant { ptr addrspace(1), ptr addrspace(1) }
// CIR-WITH-NONZERO-DEFAULT-AS-DAG: @_ZTVN10__cxxabiv117__class_type_infoE = external addrspace(1) global [0 x ptr addrspace(1)]
// CIR-WITH-NONZERO-DEFAULT-AS: define{{.*}} @_Z1fv() addrspace(4) {{.*}} personality ptr addrspace(4) @__gxx_personality_v0
