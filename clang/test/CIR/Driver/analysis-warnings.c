// RUN: %clang -### -fclangir -fclangir-analysis-warnings=return-type -c %s 2>&1 | FileCheck %s
// CHECK-NOT: argument unused
// CHECK: "-cc1"
// CHECK-SAME: "-fclangir-analysis-warnings=return-type"

// RUN: not %clang -### -fclangir -fclangir-analysis-warnings=return-type,unknown -c %s 2>&1 | FileCheck %s --check-prefix=INVALID
// INVALID: error: unsupported argument 'unknown' to option '-fclangir-analysis-warnings='
