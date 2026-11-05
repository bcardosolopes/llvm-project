// RUN: %clang_cc1 -triple armv7-none-linux-androideabi -mfloat-abi softfp -fclangir -emit-llvm %s -o %t.arm.ll
// RUN: FileCheck --input-file=%t.arm.ll %s --check-prefix=ARM
// RUN: %clang_cc1 -triple aarch64-none-linux-android -fclangir -emit-llvm %s -o %t.a64.ll
// RUN: FileCheck --input-file=%t.a64.ll %s --check-prefix=AARCH64

// The 32-bit ARM C++ ABI requires constructors and (non-deleting) destructors
// to return the 'this' pointer (carrying the `returned` attribute). AArch64 and
// the generic Itanium ABI return void.

struct S {
  S(int);
  ~S();
  int x;
};
S::S(int v) : x(v) {}
S::~S() {}

// ARM: define dso_local noundef ptr @_ZN1SC2Ei(ptr noundef nonnull returned align 4 dereferenceable(4) %{{.*}}, i32 noundef %{{.*}})
// ARM: define dso_local noundef ptr @_ZN1SD2Ev(ptr noundef nonnull returned align 4 dereferenceable(4) %{{.*}})

// AARCH64: define dso_local void @_ZN1SC2Ei(ptr noundef nonnull align 4 dereferenceable(4) %{{.*}}, i32 noundef %{{.*}})
// AARCH64: define dso_local void @_ZN1SD2Ev(ptr noundef nonnull align 4 dereferenceable(4) %{{.*}})
