// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -O0 \
// RUN:   -fprofile-instrument=clang -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu \
// RUN:   -fprofile-instrument=clang -disable-llvm-passes -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=RAW
// The name table is zlib-compressed whenever the build has zlib, which hides
// the function names from FileCheck; read it with compression turned off.
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -O0 \
// RUN:   -fprofile-instrument=clang -mllvm -enable-name-compression=false \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=NAMES

// An internal-linkage (static) function with more than one region counter must
// share a SINGLE @__profc_/@__profd_ counter array across all of its counters,
// exactly like an external-linkage function. Its PGO name is file-prefixed
// (e.g. "<unknown>:static_two"), so the FuncNameVar global name has its illegal
// ':'/'<'/'>' chars sanitized. Because the downstream InstrProfiling pass keys
// the per-function counter array off the @__profn_ name-var POINTER, every
// counter increment in the function must reference the same @__profn_ global;
// otherwise two distinct @__profc_/@__profd_ arrays (one uniqued as ".1") are
// emitted, the function's counters are split across them, and the function name
// is listed twice in @__llvm_prf_nm -- corrupting coverage and profile merges.
// This previously regressed for internal linkage because the CIR->LLVM lowering
// looked up the FuncNameVar with a name computed from the wrong linkage, so the
// lookup never matched the created (sanitized) global and each counter created a
// fresh ".1" duplicate.

// Exactly one counter array + data record per function, and no ".1" duplicate.
// CHECK-DAG: @__profc__unknown__static_two = private global [2 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK-DAG: @__profd__unknown__static_two = private global
// CHECK-DAG: @__profc__unknown__static_one = private global [1 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK-DAG: @__profc_external_two = private global [2 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK-NOT: @__profc__unknown__static_two.{{[0-9]}}
// CHECK-NOT: @__profd__unknown__static_two.{{[0-9]}}

// Each function is listed exactly once in the profile name table; pinning the
// whole table is what rules out a second "<unknown>:static_two" entry.
// NAMES: @__llvm_prf_nm = private constant {{.*}}c";\00external_two\01<unknown>:static_two\01<unknown>:static_one\01main", section "__llvm_prf_names"

// The pre-lowering intrinsics for the internal function point at ONE shared
// @__profn_ name global for both its entry (index 0) and branch (index 1)
// counters -- never a ".1"-uniqued duplicate.
// RAW-NOT: @__profn__unknown__static_two.{{[0-9]}}

// Function bodies are checked in the order CIR emits them: external_two first,
// then the internal functions.

// External-linkage control: already correct, must not regress.
int external_two(int x) {
  // CHECK-LABEL: define dso_local i32 @external_two(
  // CHECK: %pgocount = load i64, ptr @__profc_external_two
  // CHECK: %pgocount{{[0-9]*}} = load i64, ptr getelementptr inbounds ([2 x i64], ptr @__profc_external_two, i32 0, i32 1)
  if (x > 0)
    return x + 1;
  return x - 1;
}

static int static_two(int x) {
  // Entry counter (index 0) and if-then counter (index 1) share one array.
  // CHECK-LABEL: define internal i32 @static_two(
  // CHECK: %pgocount = load i64, ptr @__profc__unknown__static_two
  // CHECK: %pgocount{{[0-9]*}} = load i64, ptr getelementptr inbounds ([2 x i64], ptr @__profc__unknown__static_two, i32 0, i32 1)
  // RAW-LABEL: define internal i32 @static_two(
  // RAW: call void @llvm.instrprof.increment(ptr {{[^,]*}}@__profn__unknown__static_two, i64 {{[^,]*}}, i32 {{[^,]*}}2, i32 {{[^,]*}}0)
  // RAW: call void @llvm.instrprof.increment(ptr {{[^,]*}}@__profn__unknown__static_two, i64 {{[^,]*}}, i32 {{[^,]*}}2, i32 {{[^,]*}}1)
  if (x > 0)
    return x + 1;
  return x - 1;
}

// Single-counter internal function: must be unaffected (one array, one slot).
static int static_one(int x) {
  // CHECK-LABEL: define internal i32 @static_one(
  // CHECK: %pgocount = load i64, ptr @__profc__unknown__static_one
  return x + 1;
}

int main(void) {
  return static_two(3) + static_one(2) + external_two(4);
}
