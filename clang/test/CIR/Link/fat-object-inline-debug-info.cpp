// When cir-link inlines across TUs, a callee's local that stays in memory --
// here a parameter whose address is taken -- moves into the caller with its
// location still in the callee, inlined at the call. Its variable belongs to
// the callee's subprogram too; scoped in the caller instead, LLVM's DIBuilder
// asserts that a declare and its variable disagree. peek also stays out of
// line, and both copies share its one subprogram.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O2 -g -c %t/peek.cpp -o %t/peek.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O2 -g -c %t/use.cpp -o %t/use.o
// RUN: cir-link %t/peek.o %t/use.o --inline --emit-llvm --emit-llvm-text \
// RUN:     -o %t/out.ll
// RUN: FileCheck --input-file=%t/out.ll %s
//
// CHECK:       define {{.*}}i32 @_Z4peeki({{.*}}!dbg ![[PEEK_SP:[0-9]+]]
// CHECK-LABEL: define {{.*}}i32 @_Z3usei(
// CHECK-NOT:     call {{.*}}@_Z4peeki(
// CHECK:         call {{.*}}@_Z4sinkPi({{.*}}), !dbg ![[IN_PEEK:[0-9]+]]
// CHECK-DAG: ![[IN_PEEK]] = !DILocation({{.*}}scope: ![[PEEK_SP]], inlinedAt:
// CHECK-DAG: !DILocalVariable(name: "k", arg: 1, scope: ![[PEEK_SP]]

//--- peek.cpp
int sink(int *);
int peek(int k) { return sink(&k); }

//--- use.cpp
int peek(int k);
int use(int y) { return peek(y + 1) * 2; }
