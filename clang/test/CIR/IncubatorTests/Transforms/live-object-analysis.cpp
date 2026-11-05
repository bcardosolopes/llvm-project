// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o - | cir-opt  --pass-pipeline='builtin.module(cir.func(cir-live-object-diagnostics))' --verify-diagnostics -o /dev/null
// Trivial copy/move ctors are emitted as cir.copy and currently do not produce
// "last use of this" remarks on the defaulted ctor declarations.

struct Node {
  Node() = default;
  Node(Node &&) = default;
  Node(const Node &) = default;
  Node &operator=(Node &&);
  Node &operator=(const Node &);

  int val;
};

// Keep assignment operators non-trivial so the diagnostics still run on
// explicit special-member bodies instead of raw cir.copy sites.
// expected-remark@below {{last use of this}}
Node &Node::operator=(Node &&) = default;
// expected-remark@below {{last use of this}}
Node &Node::operator=(const Node &) = default;

int test_copy_ctor() {
  Node orig;
  Node copy(orig);
  // expected-remark@above {{last use of orig}}

  return copy.val;
  // expected-remark@above {{last use of copy}}
}

int test_move_ctor() {
  Node orig;
  Node move((Node &&)orig);
  // expected-remark@above {{last use of orig}}

  return move.val;
  // expected-remark@above {{last use of move}}
}

int test_copy_assign() {
  Node orig, copy;
  copy = orig;
  // expected-remark@above {{last use of orig}}

  return copy.val;
  // expected-remark@above {{last use of copy}}
}

int test_move_assign() {
  Node orig, move;
  move = (Node &&)orig;
  // expected-remark@above {{last use of orig}}

  return move.val;
  // expected-remark@above {{last use of move}}
}

int test_move_chain() {
  Node first, second, third, fourth;

  second = first;
  // expected-remark@above {{last use of first}}
  third = second;
  // expected-remark@above {{last use of second}}
  fourth = third;
  // expected-remark@above {{last use of third}}

  return fourth.val;
  // expected-remark@above {{last use of fourth}}
}
