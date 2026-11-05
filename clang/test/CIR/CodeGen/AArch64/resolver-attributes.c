// RUN: %clang_cc1 -triple aarch64-linux-gnu -mbranch-target-enforce -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=BTI --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -mbranch-target-enforce -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=BTI --input-file=%t.ogcg.ll %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm %s -o %t.nobti.ll
// RUN: FileCheck --check-prefix=NOBTI --input-file=%t.nobti.ll %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -emit-llvm %s -o %t.nobti.ogcg.ll
// RUN: FileCheck --check-prefix=NOBTI --input-file=%t.nobti.ogcg.ll %s

// Verify that resolver functions get correct attributes:
// - Auto-generated FMV resolvers get disable_sanitizer_instrumentation and
//   default target-specific attrs (e.g. branch-target-enforcement).
// - User-defined ifunc resolvers get disable_sanitizer_instrumentation.

__attribute__((target_clones("crc", "default")))
int global_target_clones(void) { return 0; }

__attribute__((target_version("crc")))     int global_target_version(void) { return 0; }
__attribute__((target_version("default"))) int global_target_version(void) { return 0; }

int ifunc_func(void) { return 0; }
__attribute__((target("branch-protection=bti"))) void *ifunc_resolver(void) { return ifunc_func; }
__attribute__((ifunc("ifunc_resolver"))) int ifunc(void);

// BTI: define{{.*}} ptr @ifunc_resolver() #[[#IFUNC_RESOLVER:]]
// BTI: define weak_odr{{.*}} ptr @global_target_clones.resolver() #[[#RESOLVER:]]
// BTI: define weak_odr{{.*}} ptr @global_target_version.resolver() #[[#RESOLVER]]

// BTI-DAG: attributes #[[#IFUNC_RESOLVER]] = { {{.*}}disable_sanitizer_instrumentation{{.*}}"branch-target-enforcement"
// BTI-DAG: attributes #[[#RESOLVER]] = { disable_sanitizer_instrumentation "branch-target-enforcement" }

// NOBTI: define{{.*}} ptr @ifunc_resolver() #[[#IFUNC_RESOLVER:]]
// NOBTI: define weak_odr{{.*}} ptr @global_target_clones.resolver() #[[#RESOLVER:]]
// NOBTI: define weak_odr{{.*}} ptr @global_target_version.resolver() #[[#RESOLVER]]

// NOBTI-DAG: attributes #[[#IFUNC_RESOLVER]] = { {{.*}}disable_sanitizer_instrumentation
// NOBTI-DAG: attributes #[[#RESOLVER]] = { disable_sanitizer_instrumentation }
