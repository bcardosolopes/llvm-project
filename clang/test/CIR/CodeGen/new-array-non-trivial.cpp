// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll

// Test that new T[N] works for types with non-trivial constructors/destructors.
// Previously failed with "partial array cleanups" or "trivial ctor zero-init" NYI.

struct Item {
    int id;
    Item() : id(0) {}
    ~Item();
};

// CHECK-LABEL: cir.func{{.*}} @_Z16test_array_new_3v
// CHECK:   cir.call @_Znam
Item *test_array_new_3() {
    return new Item[3];
}

// Value-initialization with trivial ctor should use memset.
struct Pod {
    int x;
    int y;
};

// CHECK-LABEL: cir.func{{.*}} @_Z19test_pod_value_initv
// CHECK:   cir.call @_Znam
// CHECK:   cir.libc.memset
Pod *test_pod_value_init() {
    return new Pod[4]();
}

// Init list with remaining elements constructed by ctor.
struct Counted {
    int val;
    Counted() : val(0) {}
    ~Counted();
};

// CHECK-LABEL: cir.func{{.*}} @_Z25test_init_list_with_ctorsv
// CHECK:   cir.call @_Znam
Counted *test_init_list_with_ctors() {
    return new Counted[5]{Counted(), Counted()};
}
