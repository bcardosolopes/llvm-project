// RUN: %clang_cc1 -triple x86_64-apple-macosx11.0 -fobjc-runtime=macosx-11.0 -Wno-objc-root-class -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=X8664
// RUN: %clang_cc1 -triple arm64-apple-ios14.0.0 -fobjc-runtime=ios-14.0 -Wno-objc-root-class -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=ARM64

// A small C++ class with a non-trivial copy ctor / dtor is returned indirectly
// (via a hidden sret pointer) per the Itanium C++ ABI, regardless of its size.
// On x86_64 such a method must be dispatched with objc_msgSend_stret even though
// the type is only 4 bytes (the old size>128 heuristic missed this). On arm64
// there is no _stret messenger, so plain objc_msgSend is used (the hidden return
// pointer is passed in x8).

struct NT {
  int x;
  NT(const NT &);
  ~NT();
};

@interface C
-(NT)getNT;
@end

// X8664-LABEL: define void @_Z6callerP1C(
// X8664: call void @objc_msgSend_stret(ptr {{.*}}sret(%struct.NT)
// ARM64-LABEL: define void @_Z6callerP1C(
// ARM64: call void @objc_msgSend(ptr {{.*}}sret(%struct.NT)
NT caller(C *o) { return [o getNT]; }
