// RUN: %clang_cc1 -triple arm64-apple-xros1 -DTEST_XROS -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=XROS %s
// RUN: %clang_cc1 -triple x86_64-apple-driverkit19.0 -DTEST_DRIVERKIT -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=DRIVERKIT %s

void use_at_available(void) {
#ifdef TEST_XROS
  if (__builtin_available(visionOS 1.2, *))
    ;
#endif

#ifdef TEST_DRIVERKIT
  if (__builtin_available(driverkit 19.1, *))
    ;
#endif
}

// XROS: declare i32 @__isPlatformVersionAtLeast(i32, i32, i32, i32)
// XROS: call i32 @__isPlatformVersionAtLeast(i32 11, i32 1, i32 2, i32 0)
// XROS: icmp ne i32

// DRIVERKIT: declare i32 @__isPlatformVersionAtLeast(i32, i32, i32, i32)
// DRIVERKIT: call i32 @__isPlatformVersionAtLeast(i32 10, i32 19, i32 1, i32 0)
// DRIVERKIT: icmp ne i32
