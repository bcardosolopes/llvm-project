// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -Wno-objc-root-class -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -Wno-objc-root-class -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM

// A synthesized atomic strong-property accessor reads the ivar-offset global
// `OBJC_IVAR_$_<Class>.<ivar>` via cir.get_global. Its pointee type must match
// the signed element type the class-metadata path defines the global with, or
// CIR verification fails with a signed/unsigned pointee mismatch on arm64.

__attribute__((objc_root_class))
@interface Root
@end

@interface Foo : Root
@property (atomic, strong) id thing;
@end

@implementation Foo
@synthesize thing;
@end

// The offset global is a signed i32 and every get_global that reads it uses the
// matching signed pointee type.
// CIR: cir.global hidden external @OBJC_IVAR_$_Foo.thing = #cir.int<0> : !s32i
// CIR: cir.get_global @OBJC_IVAR_$_Foo.thing : !cir.ptr<!s32i>

// LLVM: @"OBJC_IVAR_$_Foo.thing" = hidden global i32 0, section "__DATA, __objc_ivar"
// LLVM: load i32, ptr @"OBJC_IVAR_$_Foo.thing"
