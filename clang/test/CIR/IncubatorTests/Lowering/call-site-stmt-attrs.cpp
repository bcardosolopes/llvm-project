// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

// Verify that [[clang::always_inline]] and [[clang::noinline]] statement
// attributes are propagated to call-site attributes, matching OGCG.

bool bar();
void f(bool, bool);

void test_alwaysinline(int i) {
  // Simple call with always_inline.
  [[clang::always_inline]] bar();
  // LLVM: call {{.*}} @_Z3barv() #[[ALWAYSINLINE:[0-9]+]]
  // OGCG: call {{.*}} @_Z3barv() #[[ALWAYSINLINE:[0-9]+]]

  // Multiple calls in one statement — all get the attribute.
  [[clang::always_inline]] f(bar(), bar());
  // LLVM: call {{.*}} @_Z3barv() #[[ALWAYSINLINE]]
  // LLVM: call {{.*}} @_Z3barv() #[[ALWAYSINLINE]]
  // LLVM: call {{.*}} @_Z1fbb({{.*}}) #[[ALWAYSINLINE]]
  // OGCG: call {{.*}} @_Z3barv() #[[ALWAYSINLINE]]
  // OGCG: call {{.*}} @_Z3barv() #[[ALWAYSINLINE]]
  // OGCG: call {{.*}} @_Z1fbb({{.*}}) #[[ALWAYSINLINE]]

  // Non-attributed call — no call-site attribute group.
  bar();
  // LLVM: call noundef zeroext i1 @_Z3barv(){{$}}
  // OGCG: call noundef zeroext i1 @_Z3barv(){{$}}

  // [[gnu::always_inline]] on a statement is NOT honored.
  [[gnu::always_inline]] bar();
  // LLVM: call noundef zeroext i1 @_Z3barv(){{$}}
  // OGCG: call noundef zeroext i1 @_Z3barv(){{$}}
}

void test_noinline(int i) {
  // Simple call with noinline.
  [[clang::noinline]] bar();
  // LLVM: call {{.*}} @_Z3barv() #[[NOINLINE:[0-9]+]]
  // OGCG: call {{.*}} @_Z3barv() #[[NOINLINE:[0-9]+]]

  // Multiple calls in one statement.
  [[clang::noinline]] f(bar(), bar());
  // LLVM: call {{.*}} @_Z3barv() #[[NOINLINE]]
  // LLVM: call {{.*}} @_Z3barv() #[[NOINLINE]]
  // LLVM: call {{.*}} @_Z1fbb({{.*}}) #[[NOINLINE]]
  // OGCG: call {{.*}} @_Z3barv() #[[NOINLINE]]
  // OGCG: call {{.*}} @_Z3barv() #[[NOINLINE]]
  // OGCG: call {{.*}} @_Z1fbb({{.*}}) #[[NOINLINE]]

  // Non-attributed call.
  bar();
  // LLVM: call noundef zeroext i1 @_Z3barv(){{$}}
  // OGCG: call noundef zeroext i1 @_Z3barv(){{$}}
}

// LLVM: attributes #[[ALWAYSINLINE]] = { alwaysinline }
// LLVM: attributes #[[NOINLINE]] = { noinline }
// OGCG: attributes #[[ALWAYSINLINE]] = { alwaysinline }
// OGCG: attributes #[[NOINLINE]] = { noinline }
