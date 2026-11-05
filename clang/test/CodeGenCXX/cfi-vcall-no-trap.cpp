// Only output llvm.assume(llvm.type.test()) if cfi-vcall is disabled and whole-program-vtables is enabled
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -flto -fvisibility=hidden -fsanitize=cfi-vcall -fwhole-program-vtables -emit-llvm -o - %s | FileCheck --check-prefix=CHECK --check-prefix=CFI %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -flto -fvisibility=hidden -fsanitize=cfi-vcall -fwhole-program-vtables -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=CHECK --check-prefix=CFI %s %}
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -flto -fvisibility=hidden -fwhole-program-vtables -emit-llvm -o - %s | FileCheck --check-prefix=CHECK --check-prefix=NOCFI %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -flto -fvisibility=hidden -fwhole-program-vtables -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=CHECK --check-prefix=NOCFI %s %}

struct S1 {
  virtual void f();
};

// CHECK: define{{.*}}s1f
// CHECK: llvm.type.test
// CFI-NOT: llvm.assume
// NOCFI: metadata !"_ZTS2S1"
// NOCFI: call void @llvm.assume(i1 {{.*}})
// NOCFI: typeIdInfo: (typeTestAssumeConstVCalls: ((vFuncId: (guid:
void s1f(S1 *s1) {
  s1->f();
}
