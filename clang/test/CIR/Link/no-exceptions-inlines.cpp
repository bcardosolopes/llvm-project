// An inline function built without C++ exceptions is kept apart from the same
// function built with them.
//
// libc++'s std::__throw_out_of_range throws, or aborts under -fno-exceptions,
// under one linkonce_odr name: upstream libc++ puts the exception mode in an
// ABI tag, and the NDK's does not. The merge kept whichever copy came first,
// and libarmerged_arservicesforwolf.so got the aborting one -- every
// vector::at out of range in the library aborted where it had thrown.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN:     -O1 -c %t/eh.cpp -o %t/eh.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN:     -O1 -fno-exceptions -c %t/noeh.cpp -o %t/noeh.o
//
// The -fno-exceptions TU first: the order that lost the throwing copy.
// RUN: cir-link %t/noeh.o %t/eh.o --emit-llvm --emit-llvm-text -o %t/ne.ll
// RUN: cir-link %t/eh.o %t/noeh.o --emit-llvm --emit-llvm-text -o %t/en.ll
// RUN: FileCheck --input-file=%t/ne.ll %s --check-prefixes=CALLS
// RUN: FileCheck --input-file=%t/en.ll %s --check-prefixes=CALLS
// RUN: FileCheck --input-file=%t/ne.ll %s --check-prefixes=THROW
// RUN: FileCheck --input-file=%t/en.ll %s --check-prefixes=THROW
// RUN: FileCheck --input-file=%t/ne.ll %s --check-prefixes=ABORT
// RUN: FileCheck --input-file=%t/en.ll %s --check-prefixes=ABORT
//
// Each caller reaches the copy of its own mode...
// CALLS-DAG: call void @_Z4failv()
// CALLS-DAG: call void @_Z4failv.cirlink.noexcept()
// ...the exception-enabled copy throws, and the other one aborts.
// THROW: define {{.*}}void @_Z4failv() {{.*}}{
// THROW-NOT: define
// THROW: call void @__cxa_throw(
// ABORT: define {{.*}}void @_Z4failv.cirlink.noexcept() {{.*}}{
// ABORT-NOT: define
// ABORT: call void @abort()

// A TU that only declares the function still reaches it. Hermes defines its
// native functions inline in one TU and takes their addresses in another that
// only declares them; with the definition renamed and nothing else defining the
// name, the link failed on an undefined symbol. The one definition there is
// serves every reference.
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN:     -O1 -fno-exceptions -c %t/def.cpp -o %t/def.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN:     -O1 -fno-exceptions -c %t/table.cpp -o %t/table.o
// RUN: cir-link %t/table.o %t/def.o --emit-llvm --emit-llvm-text -o %t/td.ll
// RUN: cir-link %t/def.o %t/table.o --emit-llvm --emit-llvm-text -o %t/dt.ll
// RUN: FileCheck --input-file=%t/td.ll %s --check-prefix=TABLE
// RUN: FileCheck --input-file=%t/dt.ll %s --check-prefix=TABLE
// TABLE: @table = {{.*}}ptr @_Z6nativev.cirlink.noexcept
// TABLE-NOT: declare {{.*}}@_Z6nativev()
// TABLE: define linkonce_odr hidden {{.*}}@_Z6nativev.cirlink.noexcept()

//--- def.cpp
[[gnu::visibility("hidden")]] inline int native() { return 7; }
int call_native() { return native(); }

//--- table.cpp
int native();
void *table[] = {(void *)native};

//--- fail.h
[[gnu::visibility("hidden"), gnu::noinline]] inline void fail() {
#if __cpp_exceptions
  throw 1;
#else
  __builtin_abort();
#endif
}

//--- eh.cpp
#include "fail.h"
int eh_caller(int x) {
  if (x)
    fail();
  return 0;
}

//--- noeh.cpp
#include "fail.h"
int noeh_caller(int x) {
  if (x)
    fail();
  return 0;
}
