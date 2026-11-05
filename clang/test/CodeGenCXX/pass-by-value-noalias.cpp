// RUN: %clang_cc1 -fpass-by-value-is-noalias -triple arm64-apple-iphoneos -emit-llvm -disable-llvm-optzns %s -o - 2>&1 | FileCheck --check-prefix=WITH_NOALIAS %s
// RUN: %if cir-support %{ %clang_cc1 -fpass-by-value-is-noalias -triple arm64-apple-iphoneos -emit-llvm -disable-llvm-optzns -fclangir -disable-llvm-passes %s -o - 2>&1 | FileCheck --check-prefix=CIR_WITH_NOALIAS %s %}
// RUN: %clang_cc1 -triple arm64-apple-iphoneos -emit-llvm -disable-llvm-optzns %s -o - 2>&1 | FileCheck --check-prefix=NO_NOALIAS %s

// A trivial struct large enough so it is not passed in registers on ARM64.
struct Foo {
  int a;
  int b;
  int c;
  int d;
  int e;
  int f;
};

// Make sure noalias is added to indirect arguments with trivially copyable types
// if -fpass-by-value-is-noalias is provided.

// WITH_NOALIAS: define{{.*}} void @_Z4take3Foo(ptr dead_on_return noalias noundef %arg)
// NO_NOALIAS: define{{.*}} void @_Z4take3Foo(ptr dead_on_return noundef %arg)
void take(Foo arg) {}

int G;

// NonTrivial is not trivially-copyable, because it has a non-trivial copy
// constructor.
struct NonTrivial {
  int a;
  int b;
  int c;
  int d;
  int e;
  int f;

  NonTrivial(const NonTrivial &Other) {
    a = G + 10 + Other.a;
  }
};

// Make sure noalias is not added to indirect arguments that are not trivially
// copyable even if -fpass-by-value-is-noalias is provided.

// WITH_NOALIAS: define{{.*}} void @_Z4take10NonTrivial(ptr dead_on_return noundef %arg)
// NO_NOALIAS:   define{{.*}} void @_Z4take10NonTrivial(ptr dead_on_return noundef %arg)
void take(NonTrivial arg) {}

// Restrict-style noalias belongs on the callee signature, not on direct call
// operands.
void takeR(int *__restrict p) {}
void testR(int *p) { takeR(p); }

// WITH_NOALIAS-LABEL: define{{.*}} void @_Z5testRPi
// WITH_NOALIAS: call void @_Z5takeRPi(ptr noundef %{{.*}})
// NO_NOALIAS-LABEL: define{{.*}} void @_Z5testRPi
// NO_NOALIAS: call void @_Z5takeRPi(ptr noundef %{{.*}})
// CIR_WITH_NOALIAS-LABEL: define{{.*}} void @_Z5testRPi
// CIR_WITH_NOALIAS: call void @_Z5takeRPi(ptr noundef %{{.*}})

// Escape examples. Pointers to the objects passed to take() may escape, depending on whether a temporary copy is created or not (e.g. due to NRVO).
struct A {
  A(A **where) : data{"hello world 1"} {
    *where = this; //Escaped pointer 1 (proposed UB?)
  }

  A() : data{"hello world 2"} {}

  char data[32];
};
A *p;

// WITH_NOALIAS: define{{.*}} void @_Z4take1A(ptr dead_on_return noalias noundef %arg)
// NO_NOALIAS: define{{.*}} void @_Z4take1A(ptr dead_on_return noundef %arg)
void take(A arg) {}

// WITH_NOALIAS: define{{.*}} void @_Z7CreateAPP1A(ptr dead_on_unwind noalias writable sret(%struct.A) align 1 %agg.result, ptr noundef %where)
// NO_NOALIAS: define{{.*}} void @_Z7CreateAPP1A(ptr dead_on_unwind noalias writable sret(%struct.A) align 1 %agg.result, ptr noundef %where)
A CreateA(A **where) {
  A justlikethis;
  *where = &justlikethis; //Escaped pointer 2 (should also be UB, then)
  return justlikethis;
}

// elsewhere, perhaps compiled by a smarter compiler that doesn't make a copy here
// WITH_NOALIAS-LABEL: define{{.*}} void @_Z4testv
// WITH_NOALIAS: call void @_Z4take1A(ptr dead_on_return noalias noundef %{{.*}})
// WITH_NOALIAS: call void @_Z7CreateAPP1A(ptr dead_on_unwind writable sret(%struct.A) align 1 %{{.*}}, ptr noundef @p)
// WITH_NOALIAS: call void @_Z4take1A(ptr dead_on_return noalias noundef %{{.*}})
// NO_NOALIAS-LABEL: define{{.*}} void @_Z4testv
// NO_NOALIAS: call void @_Z4take1A(ptr dead_on_return noundef %{{.*}})
// NO_NOALIAS: call void @_Z7CreateAPP1A(ptr dead_on_unwind writable sret(%struct.A) align 1 %{{.*}}, ptr noundef @p)
// NO_NOALIAS: call void @_Z4take1A(ptr dead_on_return noundef %{{.*}})
// CIR_WITH_NOALIAS-LABEL: define{{.*}} void @_Z4testv
// CIR_WITH_NOALIAS: call void @_Z4take1A(ptr dead_on_return noalias noundef %{{.*}})
// CIR_WITH_NOALIAS: call void @_Z7CreateAPP1A(ptr dead_on_unwind writable sret(%struct.A) align 1 %{{.*}}, ptr noundef @p)
// CIR_WITH_NOALIAS: call void @_Z4take1A(ptr dead_on_return noalias noundef %{{.*}})
void test() {
  take({&p});        // 1
  take(CreateA(&p)); // 2
}

// Indirect calls must materialize the same pass-by-value attrs through
// cir.call_arg_attrs because there is no callee FuncOp to copy from.
// WITH_NOALIAS-LABEL: define{{.*}} void @_Z12testIndirectPFv1AES_
// WITH_NOALIAS: call void %{{.*}}(ptr dead_on_return noalias noundef %{{.*}})
// NO_NOALIAS-LABEL: define{{.*}} void @_Z12testIndirectPFv1AES_
// NO_NOALIAS: call void %{{.*}}(ptr dead_on_return noundef %{{.*}})
// CIR_WITH_NOALIAS-LABEL: define{{.*}} void @_Z12testIndirectPFv1AES_
// CIR_WITH_NOALIAS: call void %{{.*}}(ptr dead_on_return noalias noundef %{{.*}})
void testIndirect(void (*fp)(A), A a) {
  fp(a);
}
