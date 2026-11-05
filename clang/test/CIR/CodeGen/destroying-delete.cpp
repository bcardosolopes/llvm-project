// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test that C++20 destroying operator delete is properly handled.
// The destroying delete takes ownership of both destruction and deallocation,
// so the delete expression should call it directly without separately
// calling the destructor.

namespace std {
  struct destroying_delete_t {
    explicit destroying_delete_t() = default;
  };
}

struct S {
  int value;
  ~S();
  void operator delete(S* p, std::destroying_delete_t);
};

// For a non-virtual destructor with destroying delete, the delete expression
// calls the destroying operator delete directly.
void test_delete(S* p) {
  delete p;
}

// CIR-LABEL: cir.func {{.*}} @_Z11test_deleteP1S
// CIR-NOT:     cir.alloca !rec_std3A3Adestroying_delete_t
// CIR:         cir.call @_ZN1SdlEPS_St19destroying_delete_t(
// CIR:       }

// LLVM-LABEL: define {{.*}} @_Z11test_deleteP1S(
// LLVM-NOT:     alloca %"struct.std::destroying_delete_t"
// LLVM:         call void @_ZN1SdlEPS_St19destroying_delete_t(
// LLVM:       }

// Test with a defined destructor. The delete expression should still call
// the destroying operator delete directly, not through the deleting destructor.
struct T {
  int value;
  ~T() {}
  void operator delete(T* p, std::destroying_delete_t);
};

void test_defined(T* p) {
  delete p;
}

// CIR-LABEL: cir.func {{.*}} @_Z12test_definedP1T
// CIR-NOT:     cir.alloca !rec_std3A3Adestroying_delete_t
// CIR:         cir.call @_ZN1TdlEPS_St19destroying_delete_t(
// CIR:       }

// LLVM-LABEL: define {{.*}} @_Z12test_definedP1T(
// LLVM-NOT:     alloca %"struct.std::destroying_delete_t"
// LLVM:         call void @_ZN1TdlEPS_St19destroying_delete_t(
// LLVM:       }
