// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o - | cir-opt --cir-points-to-diagnostics --verify-diagnostics -o /dev/null

struct Node {
  Node() = default;
  Node(Node &&) = default;
  Node(const Node &) = default;
  Node &operator=(Node &&);
  Node &operator=(const Node &);
  int val;
};

// expected-remark@below {{load { this }}}
// expected-remark@below {{store { this }}}
// expected-remark@below {{load {  }}}
// expected-remark@below {{store {  }}}
// expected-remark@below {{load { :unknown: }}}
// expected-remark@below {{store { :unknown: }}}
Node &Node::operator=(Node &&) = default;
// expected-remark@below {{load { this }}}
// expected-remark@below {{store { this }}}
// expected-remark@below {{load {  }}}
// expected-remark@below {{store {  }}}
// expected-remark@below {{load { :unknown: }}}
// expected-remark@below {{store { :unknown: }}}
Node &Node::operator=(const Node &) = default;

int test_copy_ctor() {
  Node orig;
  Node copy(orig);

  return copy.val;
  // expected-remark@above {{load { copy }}}
}

int test_move_ctor() {
  Node orig;
  Node move((Node &&)orig);

  return move.val;
  // expected-remark@above {{load { move }}}
}

int test_copy_assign() {
  Node orig, copy;
  copy = orig;

  return copy.val;
  // expected-remark@above {{load { copy }}}
}

int test_move_assign() {
  Node orig, move;
  move = (Node &&)orig;

  return move.val;
  // expected-remark@above {{load { move }}}
}
