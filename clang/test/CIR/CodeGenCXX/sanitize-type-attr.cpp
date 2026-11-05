// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm -o - %s -fsanitize=type | FileCheck --check-prefix=LLVM-TYSAN %s
// RUN: echo "src:%s" | sed -e 's/\\/\\\\/g' > %t
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm -o - %s -fsanitize=type -fsanitize-blacklist=%t | FileCheck --check-prefix=LLVM-BL %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm -o - %s -fsanitize=type | FileCheck --check-prefix=OGCG-TYSAN %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm -o - %s -fsanitize=type -fsanitize-blacklist=%t | FileCheck --check-prefix=OGCG-BL %s

int global1 = 0;
int global2 = *(int *)((char *)&global1 + 1);

// LLVM-TYSAN: define internal void @__cxx_global_var_init{{.*}} [[INIT_ATTRS:#[0-9]+]]
// LLVM-TYSAN: define internal void @_GLOBAL__sub_I_{{.*}} [[INIT_ATTRS]]
// LLVM-TYSAN: attributes [[INIT_ATTRS]] = { {{.*}}sanitize_type{{.*}} }

// LLVM-BL: define internal void @__cxx_global_var_init{{.*}} [[INIT_ATTRS:#[0-9]+]]
// LLVM-BL: define internal void @_GLOBAL__sub_I_{{.*}} [[INIT_ATTRS]]
// LLVM-BL-NOT: attributes [[INIT_ATTRS]] = { {{.*}}sanitize_type{{.*}} }

// OGCG-TYSAN: define internal void @__cxx_global_var_init{{.*}} [[INIT_ATTRS:#[0-9]+]]
// OGCG-TYSAN: define internal void @_GLOBAL__sub_I_{{.*}} [[INIT_ATTRS]]
// OGCG-TYSAN: attributes [[INIT_ATTRS]] = { {{.*}}sanitize_type{{.*}} }

// OGCG-BL: define internal void @__cxx_global_var_init{{.*}} [[INIT_ATTRS:#[0-9]+]]
// OGCG-BL: define internal void @_GLOBAL__sub_I_{{.*}} [[INIT_ATTRS]]
// OGCG-BL-NOT: attributes [[INIT_ATTRS]] = { {{.*}}sanitize_type{{.*}} }

struct SX {
  int a, b;
};
SX sx;

// LLVM-TYSAN: !llvm.tysan.globals = !{[[G1:![0-9]+]], [[G2:![0-9]+]], [[SX:![0-9]+]], [[FORCE:![0-9]+]]}
// LLVM-TYSAN-DAG: [[G1]] = !{ptr @global1, [[INT:![0-9]+]]}
// LLVM-TYSAN-DAG: [[G2]] = !{ptr @global2, [[INT]]}
// LLVM-TYSAN-DAG: [[SX]] = !{ptr @sx, [[SXTY:![0-9]+]]}
// LLVM-TYSAN-DAG: [[FORCE]] = !{ptr @force_instance, [[INT]]}
// LLVM-TYSAN-DAG: [[INT]] = !{!"int", [[CHAR:![0-9]+]], i64 0}
// LLVM-TYSAN-DAG: [[CHAR]] = !{!"omnipotent char", [[ROOT:![0-9]+]], i64 0}
// LLVM-TYSAN-DAG: [[ROOT]] = !{!"Simple C++ TBAA"}
// LLVM-TYSAN-DAG: [[SXTY]] = !{!"_ZTS2SX", [[INT]], i64 0, [[INT]], i64 4}

// LLVM-BL: !llvm.tysan.globals = !{[[G1:![0-9]+]], [[G2:![0-9]+]], [[SX:![0-9]+]], [[FORCE:![0-9]+]]}

int force_instance = 42;
