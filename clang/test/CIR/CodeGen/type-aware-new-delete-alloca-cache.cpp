// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fclangir -faligned-allocation -std=c++23 -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -faligned-allocation -std=c++23 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Two type-aware (std::type_identity) placement new/delete pairs in one function
// used to trip a heap-use-after-free in the CIR codegen. emitDeleteCall erases
// the synthesized type_identity "tag" alloca when call lowering doesn't consume
// it, but left a dangling pointer in CIRBaseBuilderTy's lastAllocaInBlock cache.
// The next allocation's getBestAllocaInsertPoint then dereferenced the freed op.
// The cache is now invalidated before the erase.

namespace std {
template <class T> struct type_identity {};
enum class align_val_t : __SIZE_TYPE__ {};
} // namespace std
using size_t = __SIZE_TYPE__;
struct Context;
struct S {
  S();
  int i;
  template <typename T>
  void *operator new(std::type_identity<T>, size_t, std::align_val_t, Context &);
  template <typename T>
  void operator delete(std::type_identity<T>, void *, size_t, std::align_val_t,
                       Context &);
  template <typename T>
  void operator delete(std::type_identity<T>, void *, size_t, std::align_val_t);
};

extern "C" void test_two_pairs(Context &Ctx) {
  S *a = new (Ctx) S;
  delete a;
  S *b = new (std::align_val_t(128), Ctx) S;
  delete b;
}

// LLVM-LABEL: @test_two_pairs
// LLVM-COUNT-2: call {{.*}} @_ZN1SnwIS_EEPv
// LLVM: ret void

// OGCG-LABEL: @test_two_pairs
// OGCG-COUNT-2: call {{.*}} @_ZN1SnwIS_EEPv
