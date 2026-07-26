// RUN: %clang_cc1 -std=c++26 -freflection -fconsteval-operations -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

// Declarations injected at namespace scope must be handed to the ASTConsumer,
// or they are parsed and then silently never emitted, producing link errors.

namespace std::meta {
  using info = decltype(^^::);
  using token_sequence = decltype(^^{ });
  consteval auto queue_injection(token_sequence) -> void;
  consteval auto queue_injection(info target_ns, token_sequence) -> void;
}
using std::meta::token_sequence;
using std::meta::queue_injection;

// Unary queue_injection at namespace scope.
consteval { queue_injection(^^{ int answer() { return 42; } int gv = 7; }); }

// CHECK-DAG: @gv = global i32 7
// CHECK-DAG: define {{.*}} i32 @_Z6answerv()

namespace Target { }

// Targeted injection into another namespace, from a function body.
int fn() {
  consteval { queue_injection(^^Target, ^^{ int targeted() { return 3; } }); }
  return 0;
}

// CHECK-DAG: define {{.*}} i32 @_ZN6Target8targetedEv()

// An injected definition must actually be emitted, not merely declared.
int use() { return answer() + gv + Target::targeted(); }

// CHECK-DAG: define {{.*}} i32 @_Z3usev()
