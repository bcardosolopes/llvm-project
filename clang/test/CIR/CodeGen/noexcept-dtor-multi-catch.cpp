// RUN: %clang_cc1 -std=c++20 -triple aarch64-none-linux-android24 -fclangir -emit-llvm -fcxx-exceptions -fexceptions -fassume-nothrow-exception-dtor %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++20 -triple aarch64-none-linux-android24 -emit-llvm -fcxx-exceptions -fexceptions -fassume-nothrow-exception-dtor %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

// Tests that a destructor with multiple try-catch blocks where catch handlers
// contain potentially-throwing calls does not produce invalid LLVM IR.
// With -fassume-nothrow-exception-dtor, the catch handlers' calls are wrapped
// in synthetic terminate TryOps. FlattenCFG must not merge their exception
// pointers with the outer catch handler's exception pointer.

struct Error {
  const char* what() const;
};

void log_msg(const char*);

struct S {
  void* buffer_;
  int fd_;

  void munmap_buf();
  void disable();
  void close_fd();

  ~S() {
    if (buffer_ != nullptr) {
      try { munmap_buf(); } catch (Error& ex) { log_msg(ex.what()); }
    }
    if (fd_ != -1) {
      try { disable(); } catch (Error& ex) { log_msg(ex.what()); }
      try { close_fd(); } catch (Error& ex) { log_msg(ex.what()); }
    }
  }
};

void foo() { S s; }

// Verify the destructor compiles and has correct structure.
// CHECK: define {{.*}}void @_ZN1SD2Ev(
// CHECK: invoke void @_ZN1S10munmap_bufEv(
// CHECK: invoke void @_ZN1S7disableEv(
// CHECK: invoke void @_ZN1S8close_fdEv(

// The terminate landing pad extracts the exception pointer from its own
// landingpad result and passes it to __clang_call_terminate.
// It must NOT reference an exception pointer from a different scope.
// CHECK: call void @__clang_call_terminate(ptr
// CHECK-NEXT: unreachable

// OGCG: define {{.*}}void @_ZN1SD2Ev(
// OGCG: invoke void @_ZN1S10munmap_bufEv(
// OGCG: invoke void @_ZN1S7disableEv(
// OGCG: invoke void @_ZN1S8close_fdEv(
// OGCG: call void @__clang_call_terminate(ptr
// OGCG-NEXT: unreachable
