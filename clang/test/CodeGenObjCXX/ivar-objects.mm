// RUN: %clang_cc1 %s -triple=x86_64-apple-darwin10 -emit-llvm -o - | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple=x86_64-apple-darwin10 -emit-llvm -disable-llvm-passes -fclangir -o - | FileCheck %s --check-prefix=CIR %}
// CHECK: -[A .cxx_construct]
// CHECK: -[A .cxx_destruct]
// CHECK: -[B .cxx_construct]
// CHECK-NOT: -[B .cxx_destruct]
// CHECK-NOT: -[C .cxx_construct]
// CHECK: -[C .cxx_destruct]
// CIR-LABEL: define internal void @"\01-[A .cxx_destruct]"(
// CIR-LABEL: define internal noundef ptr @"\01-[A .cxx_construct]"(
// CIR-LABEL: define internal void @"\01-[I position]"(
// CIR: ret void
// CIR-LABEL: define internal void @"\01-[I setPosition:]"(
// CIR: call noundef nonnull align 1 dereferenceable(1) ptr @_ZN1SaSERKS_(
// CIR: ret void
// CIR-LABEL: define internal noundef ptr @"\01-[B .cxx_construct]"(
// CIR-LABEL: define internal void @"\01-[C .cxx_destruct]"(

@interface NSObject 
- alloc;
- init;
- (void) release;
@end

extern "C" int printf(const char *, ...);

int count = 17;
struct X {
  X() : value(count++) { printf( "X::X()\n"); }
  ~X() { printf( "X::~X()\n"); }
  int value;
};

struct Y {
  Y() : value(count++) { printf( "Y::Y()\n"); }
  ~Y() { printf( "Y::~Y()\n"); }
  int value;
};

@interface Super : NSObject {
  Y yvar;
  Y yvar1;
  Y ya[3];
}
- (void)finalize;
@end

@interface A : Super {
  X xvar;
  X xvar1;
  X xvar2;
  X xa[2][2];
}

- (void)print;
- (void)finalize;
@end

@implementation Super
- (void)print {
  printf( "yvar.value = %d\n", yvar.value);
  printf( "yvar1.value = %d\n", yvar1.value);
  printf( "ya[0..2] = %d %d %d\n", ya[0].value, ya[1].value, ya[2].value);
}
- (void)finalize {}
@end

@implementation A
- (void)print {
  printf( "xvar.value = %d\n", xvar.value);
  printf( "xvar1.value = %d\n", xvar1.value);
  printf( "xvar2.value = %d\n", xvar2.value);
  printf( "xa[0..1][0..1] = %d %d %d %d\n",
                   xa[0][0].value, xa[0][1].value, xa[1][0].value, xa[1][1].value);
  [super print];
}
- (void)finalize { [super finalize]; }
@end

int main() {
  A *a = [[A alloc] init];
  [a print];
  [a release];
}

class S {
public:
	S& operator = (const S&);
};

@interface I {
  S position;
}
@property(assign, nonatomic) S position;
@end

@implementation I
	@synthesize position;
@end

// This class should have a .cxx_construct but no .cxx_destruct.
namespace test3 { struct S { S(); }; }
@implementation B {
  test3::S s;
}
@end

// This class should have a .cxx_destruct but no .cxx_construct.
namespace test4 { struct S { ~S(); }; }
@implementation C {
  test4::S s;
}
@end
