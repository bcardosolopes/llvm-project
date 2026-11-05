// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fclangir -emit-llvm %s -o - | FileCheck %s

int foo(void);

void test_captured(int x) {
  #pragma clang __debug captured
  {
    if (x)
      foo();
  }
}

// CHECK: define internal void @__captured_stmt{{(\.[0-9]+)?}}(ptr noundef %0) #[[CAPATTR:[0-9]+]]
// CHECK: define dso_local void @test_captured(i32 noundef %0) #[[FNATTR:[0-9]+]]
// CHECK: call void @__captured_stmt{{(\.[0-9]+)?}}(ptr noundef

// CHECK: attributes #[[CAPATTR]] = { noinline nounwind optnone "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-features"="+cx8,+mmx,+sse,+sse2,+x87" }
