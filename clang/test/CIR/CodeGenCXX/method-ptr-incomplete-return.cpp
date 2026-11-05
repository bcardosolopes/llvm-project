// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm -o %t.ll %s
// RUN: FileCheck %s -input-file=%t.ll

// Member function pointer with incomplete return type at first use.
// When convertFunctionTypeInternal returns a placeholder for an incomplete
// return type, emitMemberFunctionPointer must re-derive the correct type
// from the method declaration.

struct Result;  // forward declaration - incomplete at first use

struct Module {
  Result getResult(int x);
};

struct Result { int value; };  // now complete

Result Module::getResult(int x) { return {x}; }

template<class C, class R, class... Args>
void registerMethod(R (C::*fn)(Args...)) {
  // Force instantiation that uses the member function pointer type
  C obj;
  (void)(obj.*fn)(42);
}

void setup() {
  registerMethod(&Module::getResult);
}

// CHECK: define {{.*}} @_Z5setupv
// CHECK-NOT: operand type mismatch
