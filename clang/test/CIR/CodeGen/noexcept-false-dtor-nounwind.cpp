// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -O0 -disable-llvm-passes \
// RUN:   -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -O0 -disable-llvm-passes \
// RUN:   -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that functions calling noexcept(false) destructors are NOT marked
// nounwind. The destructor can throw during stack unwinding.

struct ThrowingDtor {
  ~ThrowingDtor() noexcept(false) {}
};

void test_throwing_dtor() {
  ThrowingDtor obj;
}

// The test function should NOT have nounwind:
// CIR: define {{.*}} @_Z18test_throwing_dtorv()
// CIR-NOT: nounwind
// CIR-SAME: {

// OGCG: define {{.*}} @_Z18test_throwing_dtorv()
// OGCG-NOT: nounwind
// OGCG-SAME: {
