// RUN: %clang_cc1 -triple x86_64-apple-darwin -fclangir -emit-llvm -o - %s -fsanitize=thread | FileCheck --check-prefix=LLVM-TSAN %s
// RUN: echo "src:%s" | sed -e 's/\\/\\\\/g' > %t
// RUN: %clang_cc1 -triple x86_64-apple-darwin -fclangir -emit-llvm -o - %s -fsanitize=thread -fsanitize-ignorelist=%t | FileCheck --check-prefix=LLVM-BL %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin -emit-llvm -o - %s -fsanitize=thread | FileCheck --check-prefix=OGCG-TSAN %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin -emit-llvm -o - %s -fsanitize=thread -fsanitize-ignorelist=%t | FileCheck --check-prefix=OGCG-BL %s

int global1 = 0;
int global2 = *(int *)((char *)&global1 + 1);

// LLVM-TSAN: define internal void @__cxx_global_var_init{{.*}} [[INIT_ATTRS:#[0-9]+]]
// LLVM-TSAN: define internal void @_GLOBAL__sub_I_{{.*}} [[INIT_ATTRS]]
// LLVM-TSAN: attributes [[INIT_ATTRS]] = { {{.*}}sanitize_thread{{.*}} }

// LLVM-BL: define internal void @__cxx_global_var_init{{.*}} [[INIT_ATTRS:#[0-9]+]]
// LLVM-BL: define internal void @_GLOBAL__sub_I_{{.*}} [[INIT_ATTRS]]
// LLVM-BL-NOT: attributes [[INIT_ATTRS]] = { {{.*}}sanitize_thread{{.*}} }

// OGCG-TSAN: define internal void @__cxx_global_var_init{{.*}} [[INIT_ATTRS:#[0-9]+]]
// OGCG-TSAN: define internal void @_GLOBAL__sub_I_{{.*}} [[INIT_ATTRS]]
// OGCG-TSAN: attributes [[INIT_ATTRS]] = { {{.*}}sanitize_thread{{.*}} }

// OGCG-BL: define internal void @__cxx_global_var_init{{.*}} [[INIT_ATTRS:#[0-9]+]]
// OGCG-BL: define internal void @_GLOBAL__sub_I_{{.*}} [[INIT_ATTRS]]
// OGCG-BL-NOT: attributes [[INIT_ATTRS]] = { {{.*}}sanitize_thread{{.*}} }
