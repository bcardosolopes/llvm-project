// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: not diff -u %t.ll %t.cir.ll | FileCheck %s --check-prefix=DIFF
//
//
//
// Virtual inheritance still produces known LLVM IR divergences in CIR
// translation. Keep this as a divergence test (not an XFAIL) by checking a
// few representative diff lines.
//
// CodeGen:
//   $_ZTT7Diamond = comdat any
//   $_ZTV7Diamond = comdat any
//   $_ZTI4Base = comdat any
//   @_ZTV7Diamond = linkonce_odr unnamed_addr constant {...}, comdat
//   @_ZTT7Diamond = linkonce_odr unnamed_addr constant [4 x ptr] [
//     ptr getelementptr inbounds inrange(-24, 0) ({...}, ptr @_ZTV7Diamond, ...)
//   ], comdat
//   @_ZTI4Base = linkonce_odr constant {...}, comdat
//   @_ZTS4Base = linkonce_odr constant [6 x i8] c"4Base\00", comdat
//
// CIR:
//   @_ZTV7Diamond = linkonce_odr global {...}  (no comdat, no unnamed_addr)
//   @_ZTT7Diamond = linkonce_odr global [4 x ptr] [
//     ptr getelementptr inbounds nuw (i8, ptr @_ZTV7Diamond, ...)  (no inrange)
//   ]  (no comdat, no unnamed_addr)
//   @_ZTI4Base = constant {...}  (no linkonce_odr, no comdat)
//   @_ZTS4Base = linkonce_odr global [5 x i8] c"4Base", comdat  (no \00)

// DIFF: -@_ZTI4Base = linkonce_odr constant { ptr, ptr } { ptr getelementptr inbounds (ptr, ptr @_ZTVN10__cxxabiv117__class_type_infoE, i64 2), ptr @_ZTS4Base }, comdat, align 8
// DIFF: +@_ZTI4Base = linkonce_odr constant { ptr, ptr } { ptr getelementptr inbounds (i8, ptr @_ZTVN10__cxxabiv117__class_type_infoE, i64 16), ptr @_ZTS4Base }, comdat, align 8
// DIFF: -@_ZTI8Derived2 = linkonce_odr constant { ptr, ptr, i32, i32, ptr, i64 } { ptr getelementptr inbounds (ptr, ptr @_ZTVN10__cxxabiv121__vmi_class_type_infoE, i64 2), ptr @_ZTS8Derived2, i32 0, i32 1, ptr @_ZTI4Base, i64 -6141 }, comdat, align 8
// DIFF: +@_ZTI8Derived2 = linkonce_odr constant { ptr, ptr, i32, i32, ptr, i64 } { ptr getelementptr inbounds (i8, ptr @_ZTVN10__cxxabiv121__vmi_class_type_infoE, i64 16), ptr @_ZTS8Derived2, i32 0, i32 1, ptr @_ZTI4Base, i64 -6141 }, comdat, align 8

struct Base {
    int x = 10;
};

struct Derived1 : virtual Base {
    int y = 20;
};

struct Derived2 : virtual Base {
    int z = 30;
};

struct Diamond : Derived1, Derived2 {
    int w = 40;
};

int test() {
    Diamond d;
    return d.x + d.y + d.z + d.w;
}
