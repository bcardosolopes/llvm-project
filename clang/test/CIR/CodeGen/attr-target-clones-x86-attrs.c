// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir -O0 %s -o - | FileCheck %s

__attribute__((target_clones("default", "sse4.2")))
int foo(void) {
  return 0;
}

__attribute__((target_clones("default", "arch=x86-64-v2", "arch=x86-64-v3",
                              "arch=x86-64-v4")))
int isa_level(int x) {
  return x;
}

// CHECK-DAG: define {{.*}} i32 @foo.sse4.2.[[SSE_I:[0-9]+]]() #[[SSE42:[0-9]+]]
// CHECK-DAG: define {{.*}} i32 @foo.default.[[DEF_I:[0-9]+]]() #[[DEF:[0-9]+]]
// CHECK-DAG: define {{.*}} i32 @isa_level.default.[[LVL_DEF_I:[0-9]+]](i32 noundef %{{.*}}) #[[DEF]]
// CHECK-DAG: define {{.*}} i32 @isa_level.arch_x86-64-v2.[[LVL_V2_I:[0-9]+]](i32 noundef %{{.*}}) #[[V2:[0-9]+]]
// CHECK-DAG: define {{.*}} i32 @isa_level.arch_x86-64-v3.[[LVL_V3_I:[0-9]+]](i32 noundef %{{.*}}) #[[V3:[0-9]+]]
// CHECK-DAG: define {{.*}} i32 @isa_level.arch_x86-64-v4.[[LVL_V4_I:[0-9]+]](i32 noundef %{{.*}}) #[[V4:[0-9]+]]

// CHECK: attributes #[[DEF]] = { {{.*}} "target-features"="+cx8,+mmx,+sse,+sse2,+x87" }
// CHECK: attributes #[[SSE42]] = { {{.*}} "target-features"="+crc32,+cx8,+mmx,+popcnt,+sse,+sse2,+sse3,+sse4.1,+sse4.2,+ssse3,+x87" }
// CHECK: attributes #[[V2]] = { {{.*}} "target-features"="+cmov,+crc32,+cx16,+cx8,+fxsr,+mmx,+popcnt,+sahf,+sse,+sse2,+sse3,+sse4.1,+sse4.2,+ssse3,+x87" }
// CHECK: attributes #[[V3]] = { {{.*}} "target-features"="+avx,+avx2,+bmi,+bmi2,+cmov,+crc32,+cx16,+cx8,+f16c,+fma,+fxsr,+lzcnt,+mmx,+movbe,+popcnt,+sahf,+sse,+sse2,+sse3,+sse4.1,+sse4.2,+ssse3,+x87,+xsave" }
// CHECK: attributes #[[V4]] = { {{.*}} "target-features"="+avx,+avx2,+avx512bw,+avx512cd,+avx512dq,+avx512f,+avx512vl,+bmi,+bmi2,+cmov,+crc32,+cx16,+cx8,+f16c,+fma,+fxsr,+lzcnt,+mmx,+movbe,+popcnt,+sahf,+sse,+sse2,+sse3,+sse4.1,+sse4.2,+ssse3,+x87,+xsave" }
