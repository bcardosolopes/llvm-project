// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fblocks -fclangir -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fblocks -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=OGCG

// A block's dispose helper must destroy captured non-trivial C++ objects in
// REVERSE capture order (reverse of construction), matching classic clang
// (whose GenerateDestroyHelperFunction pushes per-capture cleanups that unwind
// LIFO). ClangIR previously emitted the destroy calls in forward capture order.
// The block captures `a` (field 5) then `b` (field 6); the dispose helper must
// destroy field 6 before field 5.

struct T {
  int id;
  T(int);
  T(const T &);
  ~T();
};
typedef int (^B)(void);
B g;
void take() {
  T a(1), b(2);
  g = ^int(void){ return a.id + b.id; };
}

// The field-6 (second) capture's destructor must be called before the field-5
// (first) capture's — i.e. reverse capture order. (ClangIR interleaves gep+call;
// classic emits both geps up front then the calls in reverse — either way the
// field-6 destructor call must precede the field-5 destructor call.)

// LLVM: define {{.*}}@__destroy_helper_block_
// LLVM:   %[[F6:.*]] = getelementptr inbounds{{.*}}, i32 0, i32 6
// LLVM:   call void @_ZN1TD1Ev(ptr {{.*}}%[[F6]])
// LLVM:   %[[F5:.*]] = getelementptr inbounds{{.*}}, i32 0, i32 5
// LLVM:   call void @_ZN1TD1Ev(ptr {{.*}}%[[F5]])

// OGCG: define {{.*}}@__destroy_helper_block_
// OGCG:   %[[G5:.*]] = getelementptr inbounds{{.*}}, i32 0, i32 5
// OGCG:   %[[G6:.*]] = getelementptr inbounds{{.*}}, i32 0, i32 6
// OGCG:   call void @_ZN1TD{{[12]}}Ev(ptr {{.*}}%[[G6]])
// OGCG:   call void @_ZN1TD{{[12]}}Ev(ptr {{.*}}%[[G5]])
