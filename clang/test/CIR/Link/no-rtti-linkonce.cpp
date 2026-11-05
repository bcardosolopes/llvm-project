// A linkonce definition from a TU built with RTTI prevails over the same one
// built without it, whichever input comes first.
//
// A -fno-rtti TU's copy of a vtable has a null type_info slot, and libc++'s
// std::function policies store nullptr where the RTTI build stores
// &typeid(T). One real library kept the -fno-rtti std::function empty policy,
// so target_type() on an empty std::function in an RTTI TU read through null.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN:     -O1 -c %t/rtti.cpp -o %t/rtti.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN:     -O1 -fno-rtti -fno-exceptions \
// RUN:     -c %t/nortti.cpp -o %t/nortti.o
//
// The -fno-rtti TU first: the order that lost the type_info pointers.
// RUN: cir-link %t/nortti.o %t/rtti.o --emit-llvm --emit-llvm-text -o %t/nr.ll
// RUN: cir-link %t/rtti.o %t/nortti.o --emit-llvm --emit-llvm-text -o %t/rn.ll
// RUN: FileCheck --input-file=%t/nr.ll %s --check-prefixes=CHECK,NR
// RUN: FileCheck --input-file=%t/rn.ll %s --check-prefixes=CHECK,RN
//
// The marker is consumed, not merged into the output.
// RUN: cir-link %t/nortti.o %t/rtti.o -o %t/nr.cir
// RUN: FileCheck --input-file=%t/nr.cir %s --check-prefix=MARKER
// MARKER-NOT: cir.no_rtti

// CHECK-DAG: @_ZTV5Shape = linkonce_odr {{.*}}ptr @_ZTI5Shape, ptr @_ZN5Shape4areaEv
// CHECK-DAG: @_ZZ6policyvE1p = linkonce_odr {{.*}}{ i32 1, ptr @_ZTIv }

// Functions are not globals: the first copy stays, as the linker would keep
// it. boost::throw_exception's -fno-exceptions handler went missing from
// one real library when the RTTI copy of a function displaced it.
// NR: define linkonce_odr {{.*}}void @_Z5checki(
// NR-NOT: define
// NR: call void @_Z7onErrorv()
// RN: define linkonce_odr {{.*}}void @_Z5checki(
// RN-NOT: define
// RN: call void @__cxa_throw(

//--- shape.h
namespace std {
class type_info;
}

struct Shape {
  virtual int area() { return 0; }
};

struct Policy {
  int isNull;
  const void *typeInfo;
};

inline const Policy *policy() {
  static constexpr Policy p = {1,
#if __GXX_RTTI
                               &typeid(void)
#else
                               nullptr
#endif
  };
  return &p;
}

void onError();
[[gnu::noinline]] inline void check(int x) {
  if (x)
#if __cpp_exceptions
    throw x;
#else
    onError();
#endif
}

//--- rtti.cpp
#include "shape.h"
Shape *makeShapeRTTI() { return new Shape; }
const void *policyRTTI() { return policy(); }
void checkRTTI(int x) { check(x); }

//--- nortti.cpp
#include "shape.h"
Shape *makeShapeNoRTTI() { return new Shape; }
const void *policyNoRTTI() { return policy(); }
void checkNoRTTI(int x) { check(x); }
