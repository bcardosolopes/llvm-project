// RUN: %clang_cc1 -triple aarch64-linux-gnu -std=c++17 -fclangir -emit-llvm \
// RUN:   -O1 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -std=c++17 -emit-llvm \
// RUN:   -O1 %s -o - | FileCheck %s

// Verify that __cxa_atexit calls in _GLOBAL__sub_I_* carry nounwind,
// matching OGCG behavior.

struct G {
    int val;
    G(int v) : val(v) {}
    ~G();
};

G g1(10);
G g2(20);

// CHECK: define internal void @_GLOBAL__sub_I_global
// CHECK: call i32 @__cxa_atexit({{.*}}) #[[NOUNWIND:[0-9]+]]
// CHECK: call i32 @__cxa_atexit({{.*}}) #[[NOUNWIND]]
// CHECK: attributes #[[NOUNWIND]] = {{{.*}}nounwind{{.*}}}
