// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -Wno-objc-root-class -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -Wno-objc-root-class -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=ARM64
// RUN: %clang_cc1 -triple x86_64-apple-macosx11.0.0 -Wno-objc-root-class -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=X86_64
// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -Wno-objc-root-class -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG-ARM64
// RUN: %clang_cc1 -triple x86_64-apple-macosx11.0.0 -Wno-objc-root-class -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG-X86_64

// The Darwin non-fragile ObjC ABI emits the ivar-offset global
// `OBJC_IVAR_$_<Class>.<ivar>` from two paths: the ivar-access path (a method
// loading the offset) and the class-metadata path (the definition). Both must
// agree on the offset variable's element type and signedness, or CIR emission
// asserts / fails verification. On arm64 the type is a signed 32-bit int; on
// other targets it is a signed `long`.
//
// The offset global also always lives in the `__DATA, __objc_ivar` section (the
// ObjC runtime scans it to patch offsets at load time) and is marked `constant`
// exactly when the class layout is statically known (root class / all
// superclasses @implemented): a statically-known offset is never patched, so
// making it constant turns any runtime attempt to patch it into a crash. When
// the layout is not statically known (a superclass has no visible
// @implementation) the global stays writable. This mirrors classic CodeGen's
// CGObjCNonFragileABIMac::EmitIvarOffsetVar, checked below via the OGCG prefixes.

// A root class has a statically-known layout: its offset global is `constant`.
__attribute__((objc_root_class))
@interface KnownLayout {
  int _k;
}
- (int)g;
@end

@implementation KnownLayout
- (int)g { return _k; }
@end

// CIR: cir.global {{.*}}constant external @OBJC_IVAR_$_KnownLayout._k = #cir.int<0> : !s32i {{{.*}}section = "__DATA, __objc_ivar"}

// ARM64: @"OBJC_IVAR_$_KnownLayout._k" = constant i32 0, section "__DATA, __objc_ivar", align 4
// X86_64: @"OBJC_IVAR_$_KnownLayout._k" = constant i64 0, section "__DATA, __objc_ivar", align 8
// OGCG-ARM64: @"OBJC_IVAR_$_KnownLayout._k" = constant i32 0, section "__DATA, __objc_ivar", align 4
// OGCG-X86_64: @"OBJC_IVAR_$_KnownLayout._k" = constant i64 0, section "__DATA, __objc_ivar", align 8

// `Root` has no visible @implementation, so `Foo`'s layout is not statically
// known: its offset global stays writable but keeps the section, and the
// accessor loads the offset through the global.
__attribute__((objc_root_class))
@interface Root
@end

@interface Foo : Root {
  int _c;
}
- (int)g;
@end

@implementation Foo
- (int)g { return _c; }
@end

// The offset global and the get_global that reads it must share the same signed
// pointee type (this is what the fix makes consistent across both paths).
// CIR: cir.global external @OBJC_IVAR_$_Foo._c = #cir.int<0> : !s32i {{{.*}}section = "__DATA, __objc_ivar"}
// CIR: cir.get_global @OBJC_IVAR_$_Foo._c : !cir.ptr<!s32i>

// ARM64: @"OBJC_IVAR_$_Foo._c" = global i32 0, section "__DATA, __objc_ivar"
// ARM64: load i32, ptr @"OBJC_IVAR_$_Foo._c"

// X86_64: @"OBJC_IVAR_$_Foo._c" = global i64 0, section "__DATA, __objc_ivar"
// X86_64: load i64, ptr @"OBJC_IVAR_$_Foo._c"

// OGCG-ARM64: @"OBJC_IVAR_$_Foo._c" = global i32 0, section "__DATA, __objc_ivar"
// OGCG-ARM64: load i32, ptr @"OBJC_IVAR_$_Foo._c"

// OGCG-X86_64: @"OBJC_IVAR_$_Foo._c" = global i64 0, section "__DATA, __objc_ivar"
// OGCG-X86_64: load i64, ptr @"OBJC_IVAR_$_Foo._c"
