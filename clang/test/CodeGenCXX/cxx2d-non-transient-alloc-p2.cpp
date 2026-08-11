// RUN: %clang_cc1 -std=c++2d -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

// P4341, part 2: codegen for persisted allocations whose contents reference
// the allocation itself (directly or through a cycle). Emission must
// create the global before emitting its initializer (placeholder-first), or
// it recurses forever.

namespace std {
  template <class T>
  constexpr void mark_immutable_if_constexpr(T* p) {
    __builtin_mark_immutable_if_constexpr(
        const_cast<void*>(static_cast<const void*>(p)));
  }
}

struct Node {
  int v;
  Node* self;
  constexpr Node(int v) : v(v), self(this) {}
};

struct Holder {
  Node* p;
  constexpr Holder(int v) : p(new Node(v)) {}
  Holder(const Holder&) = delete;
  constexpr ~Holder() {
    std::mark_immutable_if_constexpr(p);
    delete p;
  }
};

constexpr Holder h(42);
const Node* get() { return h.p; }

// Self-referential allocation: emitted once, initializer points to itself.
// CHECK-DAG: @_ZL1h.__nta_0 = internal constant %struct.Node { i32 42, ptr @_ZL1h.__nta_0 }
// CHECK-DAG: @_ZL1h = internal constant %struct.Holder { ptr @_ZL1h.__nta_0 }
