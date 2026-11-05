// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj -fcxx-exceptions -fexceptions %s -o %t.o
//
// Throwing/catching pointer types previously crashed with
// "buildTypeInfo: Pointer" NYI. Fixed by implementing
// __pointer_type_info RTTI emission.

struct Error { int code; };

void throw_ptr() {
    throw new Error{42};
}

int catch_ptr() {
    try { throw_ptr(); }
    catch (Error* e) { int c = e->code; delete e; return c; }
    return 0;
}

// CIR: cir.global {{.*}} @_ZTVN10__cxxabiv119__pointer_type_infoE
// CIR: cir.global {{.*}} @_ZTIP5Error = #cir.typeinfo
// CIR: cir.throw {{.*}} @_ZTIP5Error
