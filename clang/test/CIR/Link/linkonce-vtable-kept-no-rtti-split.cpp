// A linkonce vtable stays when every input agrees on RTTI, even where some
// input only declares it.
//
// A class whose key function is inline in one header and merely declared in
// another leaves different TUs with different views: TUs seeing the inline
// definition emit a linkonce vtable, TUs seeing only the declaration emit an
// external declaration and rely on an owner that does not exist. No strong
// definition exists anywhere, so deferring the linkonce copy to its "owner"
// leaves the symbol undefined. Deferral is only for an RTTI split: a -fno-rtti
// copy beside an external declaration from a TU built with RTTI, whose owner
// lives outside the merged set.
//
// Fat objects, not .cir: the merge keys on each TU's RTTI flag, which only
// fat objects carry.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN:     -O1 -fno-rtti -c %t/def.cpp -o %t/nortti_def.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN:     -O1 -fno-rtti -c %t/decl.cpp -o %t/nortti_decl.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN:     -O1 -c %t/def.cpp -o %t/rtti_def.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN:     -O1 -c %t/decl.cpp -o %t/rtti_decl.o
//
// No RTTI split, both without: the copy stays, whichever input comes first.
// RUN: cir-link %t/nortti_def.o %t/nortti_decl.o --warn-odr-divergence \
// RUN:     --emit-llvm --emit-llvm-text -o %t/nr_nr.ll 2>&1 | FileCheck %s --check-prefix=KEPT-STATS
// RUN: cir-link %t/nortti_decl.o %t/nortti_def.o --emit-llvm \
// RUN:     --emit-llvm-text -o %t/nr_nr_rev.ll
// RUN: FileCheck --input-file=%t/nr_nr.ll %s --check-prefix=KEPT
// RUN: FileCheck --input-file=%t/nr_nr_rev.ll %s --check-prefix=KEPT
// KEPT: @_ZTV12OutputBuffer = linkonce_odr {{.*}}constant
// KEPT-STATS: 0 globals deferred to their owners
//
// No split, both with RTTI: the copy stays too.
// RUN: cir-link %t/rtti_def.o %t/rtti_decl.o --emit-llvm --emit-llvm-text \
// RUN:     -o %t/r_r.ll
// RUN: cir-link %t/rtti_decl.o %t/rtti_def.o --emit-llvm --emit-llvm-text \
// RUN:     -o %t/r_r_rev.ll
// RUN: FileCheck --input-file=%t/r_r.ll %s --check-prefix=KEPT
// RUN: FileCheck --input-file=%t/r_r_rev.ll %s --check-prefix=KEPT
//
// A split -- a -fno-rtti copy beside an RTTI declaration -- still defers, both
// orders.
// RUN: cir-link %t/nortti_def.o %t/rtti_decl.o --warn-odr-divergence \
// RUN:     --emit-llvm --emit-llvm-text -o %t/nr_r.ll 2>&1 | FileCheck %s --check-prefix=SPLIT-STATS
// RUN: cir-link %t/rtti_decl.o %t/nortti_def.o --emit-llvm --emit-llvm-text \
// RUN:     -o %t/nr_r_rev.ll
// RUN: FileCheck --input-file=%t/nr_r.ll %s --check-prefix=SPLIT
// RUN: FileCheck --input-file=%t/nr_r_rev.ll %s --check-prefix=SPLIT
// SPLIT: @_ZTV12OutputBuffer = external {{.*}}constant
// SPLIT-STATS: warning: linkonce definition of '_ZTV12OutputBuffer' dropped in favour of the external definition other inputs declare
// SPLIT-STATS: 1 globals deferred to their owners

//--- ob.h
class OutputBuffer {
  char *buf = nullptr;
public:
  OutputBuffer() = default;
  virtual ~OutputBuffer() = default;
  virtual void key();
};

//--- ob_impl.h
#include "ob.h"
inline void OutputBuffer::key() {}

//--- def.cpp
#include "ob_impl.h"
void use_def() { OutputBuffer ob; (void)ob; }

//--- decl.cpp
#include "ob.h"
void use_decl() { OutputBuffer ob; (void)ob; }
