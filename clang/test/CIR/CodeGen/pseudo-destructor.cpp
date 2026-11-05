// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

template<typename T>
void destroy(T *p) {
  p->~T();
}

void test_pseudo_dtor() {
  int x = 5;
  destroy(&x);
}

// Pseudo-destructor on scalar type should be a no-op — no call emitted.
// CHECK-LABEL: @_Z7destroyIiEvPT_
// CHECK:         cir.load
// CHECK-NEXT:    cir.return
