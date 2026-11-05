// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir -fcxx-exceptions -fexceptions %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fno-clangir -fcxx-exceptions -fexceptions %s -o - | FileCheck %s --check-prefix=OGCG

// Ensure __cxa_begin_catch call sites carry nounwind, matching OGCG.

int begin_catch_attr_check() {
  try {
    throw 1;
  } catch (...) {
    return 0;
  }
}

// LLVM-LABEL: define{{.*}} @_Z22begin_catch_attr_checkv
// LLVM: call ptr @__cxa_begin_catch(ptr {{.*}}) #[[LLVM_BC_ATTR:[0-9]+]]
// LLVM: attributes #[[LLVM_BC_ATTR]] = { nounwind }

// OGCG-LABEL: define{{.*}} @_Z22begin_catch_attr_checkv
// OGCG: call ptr @__cxa_begin_catch(ptr {{.*}}) #[[OGCG_BC_ATTR:[0-9]+]]
// OGCG: attributes #[[OGCG_BC_ATTR]] = { nounwind }
