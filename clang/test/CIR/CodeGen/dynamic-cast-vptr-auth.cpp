// Verify that on the arm64e pointer-authentication ABI, a dynamic_cast that
// goes through the __dynamic_cast runtime authenticates the source object's
// vptr (a no-op authenticating load that traps early on a forged/corrupt vptr)
// before the call, matching classic codegen (OGCG). The __dynamic_cast call
// itself is unchanged.

// RUN: %clang_cc1 -triple arm64e-apple-ios -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -fptrauth-vtable-pointer-address-discrimination \
// RUN:   -fptrauth-vtable-pointer-type-discrimination \
// RUN:   -std=c++17 -O1 -disable-llvm-passes -fclangir -emit-cir -o - %s \
// RUN:   | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple arm64e-apple-ios -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -fptrauth-vtable-pointer-address-discrimination \
// RUN:   -fptrauth-vtable-pointer-type-discrimination \
// RUN:   -std=c++17 -O1 -disable-llvm-passes -fclangir -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple arm64e-apple-ios -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -fptrauth-vtable-pointer-address-discrimination \
// RUN:   -fptrauth-vtable-pointer-type-discrimination \
// RUN:   -std=c++17 -O1 -disable-llvm-passes -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=OGCG

struct Base { virtual ~Base(); };
struct Der : Base {};

// A pointer dynamic_cast: the vptr authentication must be guarded by the same
// null check as the __dynamic_cast call, since a null source never has a vptr.
Der *ptr_cast(Base *b) { return dynamic_cast<Der *>(b); }

// CIR-LABEL: cir.func {{.*}}@_Z8ptr_castP4Base
// CIR:         %[[SRC:.*]] = cir.load {{.*}} !cir.ptr<!rec_Base>
// CIR:         %[[NOTNULL:.*]] = cir.cast ptr_to_bool %[[SRC]]
// CIR-NEXT:    cir.if %[[NOTNULL]] {
// CIR-NEXT:      %[[VPTRPTR:.*]] = cir.vtable.get_vptr %[[SRC]]
// CIR-NEXT:      %[[VPTR:.*]] = cir.load {{.*}} %[[VPTRPTR]]
// CIR-NEXT:      %[[DISCC:.*]] = cir.const #cir.int<12199>
// CIR:           %[[DISC:.*]] = cir.call_llvm_intrinsic "ptrauth.blend" %{{.*}}, %[[DISCC]]
// CIR:           %[[KEY:.*]] = cir.const #cir.int<2> : !u32i
// CIR-NEXT:      %[[AUTHED:.*]] = cir.call_llvm_intrinsic "ptrauth.auth" %{{.*}}, %[[KEY]], %[[DISC]]
// CIR:           cir.load volatile {{.*}} !u8i
// CIR:         }
// CIR:         cir.ternary
// CIR:           cir.call @__dynamic_cast(

// LLVM-LABEL: define {{.*}} ptr @_Z8ptr_castP4Base
// LLVM:         %[[SRC:[0-9]+]] = load ptr, ptr %{{[0-9]+}}, align 8
// LLVM:         %[[NN:[0-9]+]] = icmp ne ptr %[[SRC]], null
// LLVM-NEXT:    br i1 %[[NN]], label %[[AUTHBB:[0-9]+]], label
// LLVM:       [[AUTHBB]]:
// LLVM-NEXT:    %[[VT:[0-9]+]] = load ptr, ptr %[[SRC]], align 8
// LLVM-NEXT:    %[[ADDR:[0-9]+]] = ptrtoint ptr %[[SRC]] to i64
// LLVM-NEXT:    %[[D:[0-9]+]] = call i64 @llvm.ptrauth.blend(i64 %[[ADDR]], i64 12199)
// LLVM-NEXT:    %[[VTI:[0-9]+]] = ptrtoint ptr %[[VT]] to i64
// LLVM-NEXT:    %[[A:[0-9]+]] = call i64 @llvm.ptrauth.auth(i64 %[[VTI]], i32 2, i64 %[[D]])
// LLVM-NEXT:    %[[AP:[0-9]+]] = inttoptr i64 %[[A]] to ptr
// LLVM-NEXT:    load volatile i8, ptr %[[AP]]
// LLVM:         call ptr @__dynamic_cast(ptr %[[SRC]], ptr @_ZTI4Base, ptr @_ZTI3Der, i64 0)

// OGCG-LABEL: define {{.*}} ptr @_Z8ptr_castP4Base
// OGCG:       dynamic_cast.notnull:
// OGCG-NEXT:    %[[VT:.*]] = load ptr, ptr %[[SRC:[0-9]+]], align 8
// OGCG-NEXT:    %[[ADDR:[0-9]+]] = ptrtoint ptr %[[SRC]] to i64
// OGCG-NEXT:    %[[D:[0-9]+]] = call i64 @llvm.ptrauth.blend(i64 %[[ADDR]], i64 12199)
// OGCG-NEXT:    %[[VTI:[0-9]+]] = ptrtoint ptr %[[VT]] to i64
// OGCG-NEXT:    %[[A:[0-9]+]] = call i64 @llvm.ptrauth.auth(i64 %[[VTI]], i32 2, i64 %[[D]])
// OGCG-NEXT:    %[[AP:[0-9]+]] = inttoptr i64 %[[A]] to ptr
// OGCG-NEXT:    load volatile i8, ptr %[[AP]]
// OGCG:         call ptr @__dynamic_cast(ptr %[[SRC]], ptr @_ZTI4Base, ptr @_ZTI3Der, i64 0)

// A reference dynamic_cast: the source is never null, so the vptr
// authentication is emitted unconditionally, just as in classic codegen.
Der &ref_cast(Base &b) { return dynamic_cast<Der &>(b); }

// LLVM-LABEL: define {{.*}} ptr @_Z8ref_castR4Base
// LLVM:         %[[SRC:[0-9]+]] = load ptr, ptr %{{[0-9]+}}, align 8, !tbaa {{.*}}, !nonnull
// LLVM-NEXT:    %[[VT:[0-9]+]] = load ptr, ptr %[[SRC]], align 8
// LLVM-NEXT:    %[[ADDR:[0-9]+]] = ptrtoint ptr %[[SRC]] to i64
// LLVM-NEXT:    %[[D:[0-9]+]] = call i64 @llvm.ptrauth.blend(i64 %[[ADDR]], i64 12199)
// LLVM-NEXT:    %[[VTI:[0-9]+]] = ptrtoint ptr %[[VT]] to i64
// LLVM-NEXT:    %[[A:[0-9]+]] = call i64 @llvm.ptrauth.auth(i64 %[[VTI]], i32 2, i64 %[[D]])
// LLVM-NEXT:    %[[AP:[0-9]+]] = inttoptr i64 %[[A]] to ptr
// LLVM-NEXT:    load volatile i8, ptr %[[AP]]
// LLVM:         call ptr @__dynamic_cast(ptr %[[SRC]], ptr @_ZTI4Base, ptr @_ZTI3Der, i64 0)
