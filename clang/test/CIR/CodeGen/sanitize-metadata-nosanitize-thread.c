// RUN: %clang_cc1 -O -triple x86_64-gnu-linux \
// RUN:   -fexperimental-sanitize-metadata=covered \
// RUN:   -fexperimental-sanitize-metadata=atomics \
// RUN:   -fexperimental-sanitize-metadata=uar \
// RUN:   -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -O -triple x86_64-gnu-linux \
// RUN:   -fexperimental-sanitize-metadata=covered \
// RUN:   -fexperimental-sanitize-metadata=atomics \
// RUN:   -fexperimental-sanitize-metadata=uar \
// RUN:   -fclangir -emit-cir %s -o %t.cir
// RUN: cir-translate -cir-to-llvmir %t.cir | FileCheck %s --check-prefix=SERIALIZED
// RUN: %clang_cc1 -O -triple x86_64-gnu-linux \
// RUN:   -fexperimental-sanitize-metadata=covered \
// RUN:   -fexperimental-sanitize-metadata=atomics \
// RUN:   -fexperimental-sanitize-metadata=uar \
// RUN:   -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -O -triple x86_64-gnu-linux \
// RUN:   -fexperimental-sanitize-metadata=covered \
// RUN:   -fexperimental-sanitize-metadata=atomics \
// RUN:   -fexperimental-sanitize-metadata=uar \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

__attribute__((no_sanitize("thread"))) int no_thread(int *p) {
  return *p;
}

__attribute__((no_sanitize("all"))) int no_all(int *p) {
  return *p;
}

// CIR: module @{{.*}} attributes {
// CIR-SAME: cir.sanitize_binary_metadata_atomics
// CIR-SAME: cir.sanitize_binary_metadata_covered
// CIR-SAME: cir.sanitize_binary_metadata_uar

// LLVM-DAG: @__start_sanmd_covered2 = extern_weak hidden global ptr
// LLVM-DAG: @__stop_sanmd_covered2 = extern_weak hidden global ptr
// LLVM-LABEL: define{{.*}} @no_thread
// LLVM-SAME: #[[NO_SAN_ATTR:[0-9]+]] !pcsections ![[PCS:[0-9]+]]
// LLVM-LABEL: define{{.*}} @no_all
// LLVM-SAME: #[[NO_SAN_ATTR]] !pcsections ![[PCS]]
// LLVM: define hidden void @__sanitizer_metadata_covered2.module_ctor()
// LLVM: define hidden void @__sanitizer_metadata_covered2.module_dtor()
// LLVM: attributes #[[NO_SAN_ATTR]] = { {{.*}}"no_sanitize_thread"{{.*}} }
// LLVM: ![[PCS]] = !{!"sanmd_covered2!C", ![[FEATURES:[0-9]+]]}
// LLVM: ![[FEATURES]] = !{i64 0}

// SERIALIZED-DAG: @__start_sanmd_covered2 = extern_weak hidden global ptr
// SERIALIZED-DAG: @__stop_sanmd_covered2 = extern_weak hidden global ptr
// SERIALIZED-LABEL: define{{.*}} @no_thread
// SERIALIZED-SAME: #[[SER_NO_SAN_ATTR:[0-9]+]]{{.*}} !pcsections ![[PCS_SER:[0-9]+]]
// SERIALIZED-LABEL: define{{.*}} @no_all
// SERIALIZED-SAME: #[[SER_NO_SAN_ATTR]]{{.*}} !pcsections ![[PCS_SER]]
// SERIALIZED: define hidden void @__sanitizer_metadata_covered2.module_ctor()
// SERIALIZED: define hidden void @__sanitizer_metadata_covered2.module_dtor()
// SERIALIZED: attributes #[[SER_NO_SAN_ATTR:[0-9]+]] = { {{.*}}"no_sanitize_thread"{{.*}} }
// SERIALIZED: ![[PCS_SER]] = !{!"sanmd_covered2!C", ![[FEATURES_SER:[0-9]+]]}
// SERIALIZED: ![[FEATURES_SER]] = !{i64 0}

// OGCG-LABEL: define{{.*}} @no_thread
// OGCG-SAME: #[[OGCG_NO_SAN_ATTR:[0-9]+]]
// OGCG-LABEL: define{{.*}} @no_all
// OGCG-SAME: #[[OGCG_NO_SAN_ATTR]]
// OGCG: attributes #[[OGCG_NO_SAN_ATTR]] = { {{.*}}"no_sanitize_thread"{{.*}} }
