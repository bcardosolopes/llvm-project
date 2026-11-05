// RUN: %clang_cc1 -triple i386-apple-macosx10.13 -fobjc-runtime=macosx-10.13 -Wno-objc-root-class -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=I386
// RUN: %clang_cc1 -triple x86_64-apple-macosx11.0 -fobjc-runtime=macosx-11.0 -Wno-objc-root-class -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=X8664

// A method whose result is returned indirectly (via a hidden sret pointer) must
// be dispatched with objc_msgSend_stret, not plain objc_msgSend: the plain
// messenger does not forward the hidden struct-return pointer, so self/_cmd
// would land in the wrong argument slots. On i386 _Complex double and
// _Complex long double are returned indirectly, so both need the _stret
// messenger even though neither is a struct/record type. On x86_64 they are
// returned directly (registers / fp2ret) and must NOT use _stret.

@interface C
-(_Complex double)cd;
-(_Complex long double)cld;
@end

// I386-LABEL: define void @getCD(
// I386: call void @objc_msgSend_stret(ptr {{.*}}sret({ double, double })
// X8664-LABEL: define { double, double } @getCD(
// X8664: call { double, double } @objc_msgSend(
_Complex double getCD(C *o) { return [o cd]; }

// I386-LABEL: define void @getCLD(
// I386: call void @objc_msgSend_stret(ptr {{.*}}sret({ x86_fp80, x86_fp80 })
// X8664-LABEL: define { x86_fp80, x86_fp80 } @getCLD(
// X8664: call { x86_fp80, x86_fp80 } @objc_msgSend_fp2ret(
_Complex long double getCLD(C *o) { return [o cld]; }
