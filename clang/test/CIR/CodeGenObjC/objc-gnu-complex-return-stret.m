// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fobjc-runtime=gnustep-2.0 -fobjc-dispatch-method=non-legacy -Wno-objc-root-class -fclangir -emit-llvm -o - %s | FileCheck %s

// GNU-family runtime sibling of objc-complex-return-stret.m: a method whose
// result is returned indirectly (sret) must use the struct-return messenger
// even when the return type is not a struct/record. On i386 _Complex double
// and _Complex long double are returned indirectly, so both need
// objc_msgSend_stret. The GNU non-legacy path previously only consulted the
// true return ABI for struct/class types, so _Complex fell back to a
// size>128 && isStructureOrClass heuristic and wrongly used plain objc_msgSend.

@interface C
-(_Complex double)cd;
-(_Complex long double)cld;
@end

// CHECK-LABEL: define {{.*}}void @getCD(
// CHECK: call void @objc_msgSend_stret(ptr {{.*}}sret({ double, double })
_Complex double getCD(C *o) { return [o cd]; }

// CHECK-LABEL: define {{.*}}void @getCLD(
// CHECK: call void @objc_msgSend_stret(ptr {{.*}}sret({ x86_fp80, x86_fp80 })
_Complex long double getCLD(C *o) { return [o cld]; }
