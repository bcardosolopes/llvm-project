// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// cpu_specific variant: atom gets mangling character 'O'.
__attribute__((cpu_specific(atom)))
void SingleVersion(void) {}

// cpu_dispatch triggers resolver/ifunc generation.
__attribute__((cpu_dispatch(atom)))
void SingleVersion(void);

// CIR: cir.func {{.*}} @SingleVersion.O()
// CIR: cir.func comdat weak_odr {{.*}} @SingleVersion.resolver() -> !cir.ptr<!cir.func<()>>
// CIR:   cir.call @__cpu_indicator_init
// CIR:   cir.brcond {{.*}} ^bb1, ^bb2
// CIR: ^bb1:
// CIR:   cir.return {{.*}} : !cir.ptr<!cir.func<()>>
// CIR: ^bb2:
// CIR:   cir.trap
// CIR: cir.func weak_odr {{.*}} @SingleVersion() alias(@SingleVersion.resolver) {{.*}} attributes {cir.ifunc}

// Two cpu_specific versions with a cpu_dispatch.
__attribute__((cpu_specific(atom)))
void TwoVersions(void) {}
__attribute__((cpu_specific(ivybridge)))
void TwoVersions(void) {}
__attribute__((cpu_dispatch(atom, ivybridge)))
void TwoVersions(void);

// CIR: cir.func {{.*}} @TwoVersions.O()
// CIR: cir.func {{.*}} @TwoVersions.S()
// CIR: cir.func comdat weak_odr {{.*}} @TwoVersions.resolver() -> !cir.ptr<!cir.func<()>>
// CIR:   cir.call @__cpu_indicator_init
// CIR:   cir.brcond {{.*}} ^bb1, ^bb2
// CIR: ^bb1:
// CIR:   cir.return {{.*}} : !cir.ptr<!cir.func<()>>
// CIR: ^bb2:
// CIR:   cir.brcond {{.*}} ^bb3, ^bb4
// CIR: ^bb3:
// CIR:   cir.return {{.*}} : !cir.ptr<!cir.func<()>>
// CIR: ^bb4:
// CIR:   cir.trap
// CIR: cir.func weak_odr {{.*}} @TwoVersions() alias(@TwoVersions.resolver) {{.*}} attributes {cir.ifunc}

// Call site: calls go through the ifunc (base name).
void caller(void) {
  TwoVersions();
}

// CIR: cir.func {{.*}} @caller()
// CIR:   cir.call @TwoVersions()

// LLVM output: aliases + ifuncs at top, then definitions.
// LLVM: @SingleVersion = weak_odr alias void (), ptr @SingleVersion.ifunc
// LLVM: @TwoVersions = weak_odr alias void (), ptr @TwoVersions.ifunc
// LLVM: @SingleVersion.ifunc = weak_odr ifunc void (), ptr @SingleVersion.resolver
// LLVM: @TwoVersions.ifunc = weak_odr ifunc void (), ptr @TwoVersions.resolver
// LLVM: define {{.*}} void @SingleVersion.O()
// LLVM: define weak_odr{{.*}} ptr @SingleVersion.resolver()
// LLVM:   call void @__cpu_indicator_init
// LLVM:   br i1 {{.*}}, label %{{.*}}, label %{{.*}}
// LLVM:   ret ptr @SingleVersion.O
// LLVM:   call void @llvm.trap()
// LLVM:   unreachable
// LLVM: define {{.*}} void @TwoVersions.O()
// LLVM: define {{.*}} void @TwoVersions.S()
// LLVM: define weak_odr{{.*}} ptr @TwoVersions.resolver()
// LLVM:   call void @__cpu_indicator_init
// LLVM:   br i1 {{.*}}, label %{{.*}}, label %{{.*}}
// LLVM:   ret ptr @TwoVersions.S
// LLVM:   br i1 {{.*}}, label %{{.*}}, label %{{.*}}
// LLVM:   ret ptr @TwoVersions.O
// LLVM:   call void @llvm.trap()
// LLVM:   unreachable
// LLVM: define {{.*}} void @caller()
// LLVM:   call void @TwoVersions()
