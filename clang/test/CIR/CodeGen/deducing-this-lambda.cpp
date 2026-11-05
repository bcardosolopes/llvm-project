// RUN: %clang_cc1 -std=c++23 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s < %t.cir
// RUN: %clang_cc1 -std=c++23 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s < %t.ll

// C++23 deducing-this lambdas with captures.

// Value capture with explicit object parameter by reference.
int test_value_capture() {
  int x = 10;
  auto f = [x](this auto& self) -> int { return x; };
  return f();
}

// The lambda's operator() takes the closure by reference (this auto& self).
// The explicit object parameter is used to derive the closure object for
// capture access, instead of the implicit 'this' pointer.

// CIR-LABEL: cir.func {{.*}} lambda {{.*}}@_ZZ18test_value_capture
// CIR:   cir.alloca !cir.ptr<!rec_anon{{.*}}>, {{.*}}["self"
// CIR:   cir.load
// CIR:   cir.get_member %{{.*}}[0] {name = "x"}
// CIR:   cir.return

// LLVM: define {{.*}} @"_ZZ18test_value_capture
// LLVM:   getelementptr inbounds nuw %class.anon{{.*}}, ptr %{{.*}}, i32 0, i32 0
// LLVM:   ret i32

// Reference capture with explicit object parameter by reference.
int test_ref_capture() {
  int x = 20;
  auto f = [&x](this auto& self) -> int { return x; };
  return f();
}

// CIR-LABEL: cir.func {{.*}} lambda {{.*}}@_ZZ16test_ref_capture
// CIR:   cir.alloca !cir.ptr<!rec_anon{{.*}}>, {{.*}}["self"
// CIR:   cir.get_member %{{.*}}[0] {name = "x"}
// CIR:   cir.load
// CIR:   cir.load
// CIR:   cir.return

// LLVM: define {{.*}} @"_ZZ16test_ref_capture
// LLVM:   getelementptr inbounds nuw %class.anon{{.*}}, ptr %{{.*}}, i32 0, i32 0
// LLVM:   ret i32

// Capturing 'this' pointer in a deducing-this lambda.
struct S {
  int val = 42;
  auto get() { return [this](this auto self) -> int { return val; }; }
};

int test_this_capture() {
  S s;
  return s.get()();
}

// The lambda operator() takes the closure by value. The captured 'this'
// is loaded from the closure's first field and used to access 'val'.

// CIR-LABEL: cir.func {{.*}} lambda {{.*}}@_ZZN1S3getEvENHUlT_E_clIS1_EEiS0_
// CIR:   cir.get_member %{{.*}}[0]
// CIR:   cir.load
// CIR:   cir.get_member %{{.*}}[0] {name = "val"}
// CIR:   cir.return

// LLVM: define {{.*}} @_ZZN1S3getEvENHUlT_E_clIS1_EEiS0_
// LLVM:   getelementptr inbounds nuw %class.anon{{.*}}, ptr %{{.*}}, i32 0, i32 0
// LLVM:   load ptr
// LLVM:   getelementptr inbounds nuw %struct.S, ptr %{{.*}}, i32 0, i32 0
// LLVM:   ret i32

// Recursive deducing-this lambda (no captures — baseline).
int test_recursive() {
  auto fib = [](this auto& self, int n) -> int {
    if (n <= 1) return n;
    return self(n - 1) + self(n - 2);
  };
  return fib(5);
}

// CIR-LABEL: cir.func {{.*}}@_ZZ14test_recursive
// CIR:   cir.return

// LLVM: define {{.*}} @"_ZZ14test_recursive
// LLVM:   ret i32
