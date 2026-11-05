// The calling convention of C++ records survives the fat object.
//
// Whether a record goes in registers is not in its layout. One with a
// non-trivial copy constructor or destructor is passed by invisible reference
// and returned through sret, and when its destructor is non-trivial the caller
// destroys the argument, so the memory is not dead on return. Target lowering
// read that off the record's clang AST -- which the CIR a fat object carries
// does not have. cir-link lowered such records as if they were plain data:
// take_nt(NT) took an i64 and make_nt() returned one, where every caller built
// by clang, in any other library, passes a pointer and expects sret. A record
// holding only a vptr (std::exception's shape) went further, into coercing a
// vptr as an integer, and stopped a real library on an assertion.
//
// clang now records those facts as cir.record_abi when it captures the CIR,
// and cir-link lowers from them as clang lowers from the AST.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -c %t/def.cpp -o %t/def.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -c %t/use.cpp -o %t/use.o
//
// The facts, as captured and as merged.
// RUN: cir-link %t/def.o %t/use.o -o %t/merged.cir
// RUN: cir-opt %t/merged.cir | FileCheck %s --check-prefix=FACTS
// FACTS: module {{.*}}cir.record_abi = {NT = 3 : i32, VP = 3 : i32}
//
// cir-link, in both orders, and clang lowering each TU itself, agree.
// RUN: cir-link %t/def.o %t/use.o --emit-llvm --emit-llvm-text -o %t/du.ll
// RUN: cir-link %t/use.o %t/def.o --emit-llvm --emit-llvm-text -o %t/ud.ll
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN: -O1 -S -emit-llvm %t/def.cpp -o %t/def.ll
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN: -O1 -S -emit-llvm %t/use.cpp -o %t/use.ll
// RUN: FileCheck --input-file=%t/du.ll %s --check-prefixes=DEF,USE
// RUN: FileCheck --input-file=%t/ud.ll %s --check-prefixes=DEF,USE
// RUN: FileCheck --input-file=%t/def.ll %s --check-prefix=DEF
// RUN: FileCheck --input-file=%t/use.ll %s --check-prefix=USE
//
// A non-trivial record: by invisible reference, not dead on return, and sret.
// DEF-DAG: define {{.*}}i64 @_Z7take_nt2NT(ptr noundef {{(readonly captures\(none\) )?}}%0)
// DEF-DAG: define {{.*}}void @_Z7make_ntv(ptr dead_on_unwind noalias {{(nonnull )?}}writable sret(%struct.NT)
// A record holding only a vptr: by invisible reference.
// DEF-DAG: define {{.*}}i32 @_Z7take_vp2VP(ptr noundef
// A trivially copyable one too large for registers: by reference, and dead on
// return, which is what the non-trivial ones must not be.
// DEF-DAG: define {{.*}}i64 @_Z8take_big3Big(ptr dead_on_return noundef
// USE-DAG: call void @_Z7make_ntv(ptr dead_on_unwind {{(nonnull )?}}writable sret(%struct.NT)

//--- def.cpp
struct NT { NT(); NT(const NT &); ~NT(); long v; };
struct VP { virtual ~VP(); };
struct Big { long a, b, c; };
long take_nt(NT n) { return n.v; }
NT make_nt() { return NT(); }
int take_vp(VP v) { return 1; }
long take_big(Big b) { return b.a; }

//--- use.cpp
struct NT { NT(); NT(const NT &); ~NT(); long v; };
struct VP { virtual ~VP(); };
struct Big { long a, b, c; };
long take_nt(NT n);
NT make_nt();
int take_vp(VP v);
long take_big(Big b);
long use(VP &vp) {
  Big b{1, 2, 3};
  return take_nt(make_nt()) + take_vp(vp) + take_big(b);
}
