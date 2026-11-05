// RUN: %clang_cc1 -triple x86_64-unknown-linux -fclangir \
// RUN:   -fprofile-instrument=clang -emit-llvm -o - %s | FileCheck %s

// Functions defined in system headers must not be PGO-instrumented (matching
// classic CodeGen). Previously CIR instrumented every function it emitted,
// including inline STL template helpers pulled in from system headers, which
// bloated profiles with dozens of extra counters per standard-library use.
// The `# <line> "file" <flags>` marker with flag 3 marks the following code as
// coming from a system header.

# 1 "sysheader.h" 1 3
static int in_system_header(int x) { return x + 1; }
# 13 "pgo-system-header-exclusion.c" 2
int in_main_file(int x) { return in_system_header(x) * 2; }

// The main-file function is instrumented; the system-header function is not.
// CHECK-DAG: @__profc_in_main_file =
// CHECK-NOT: @__profc_in_system_header
