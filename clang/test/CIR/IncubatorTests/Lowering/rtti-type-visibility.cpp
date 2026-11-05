// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// Test that CIR correctly propagates type_visibility to RTTI globals.

#define TYPE_HIDDEN __attribute__((type_visibility("hidden")))
#define TYPE_DEFAULT __attribute__((type_visibility("default")))
#define DEFAULT __attribute__((visibility("default")))

// Type with hidden type_visibility — RTTI globals should be hidden.
namespace test_hidden {
  struct TYPE_HIDDEN A {
    virtual void foo();
  };
  void A::foo() {}
}
// LLVM-DAG: @_ZTSN11test_hidden1AE = hidden constant
// LLVM-DAG: @_ZTIN11test_hidden1AE = hidden constant
// OGCG-DAG: @_ZTSN11test_hidden1AE = hidden constant
// OGCG-DAG: @_ZTIN11test_hidden1AE = hidden constant

// Type with default visibility + hidden type_visibility — type_visibility
// overrides for RTTI.
namespace test_default_type_hidden {
  struct DEFAULT TYPE_HIDDEN A {
    virtual void foo();
  };
  void A::foo() {}
}
// LLVM-DAG: @_ZTSN24test_default_type_hidden1AE = hidden constant
// LLVM-DAG: @_ZTIN24test_default_type_hidden1AE = hidden constant
// OGCG-DAG: @_ZTSN24test_default_type_hidden1AE = hidden constant
// OGCG-DAG: @_ZTIN24test_default_type_hidden1AE = hidden constant

// Type with default type_visibility — RTTI globals should have default
// visibility.
namespace test_type_default {
  struct TYPE_DEFAULT A {
    virtual void foo();
  };
  void A::foo() {}
}
// LLVM-DAG: @_ZTSN17test_type_default1AE = constant
// LLVM-DAG: @_ZTIN17test_type_default1AE = constant
// OGCG-DAG: @_ZTSN17test_type_default1AE = constant
// OGCG-DAG: @_ZTIN17test_type_default1AE = constant

// Template with TYPE_HIDDEN argument — RTTI globals should be hidden.
namespace test_template {
  struct TYPE_HIDDEN A;
  template <class T> struct TYPE_DEFAULT B {
    virtual void foo() {}
  };
  template struct B<A>;
}
// LLVM-DAG: @_ZTSN13test_template1BINS_1AEEE = weak_odr hidden constant
// LLVM-DAG: @_ZTIN13test_template1BINS_1AEEE = weak_odr hidden constant
// OGCG-DAG: @_ZTSN13test_template1BINS_1AEEE = weak_odr hidden constant
// OGCG-DAG: @_ZTIN13test_template1BINS_1AEEE = weak_odr hidden constant
