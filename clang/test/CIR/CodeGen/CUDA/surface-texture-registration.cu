// Host-side registration of CUDA device builtin surface and texture
// references, and the device-side handle type they are lowered to. Verify
// ClangIR matches classic CodeGen (OGCG).

// RUN: echo "GPU binary would be here" > %t.fatbin

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:            -fcuda-include-gpubinary %t.fatbin -emit-llvm \
// RUN:            -target-sdk-version=12.3 %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM-HOST --input-file=%t.ll %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu \
// RUN:            -fcuda-include-gpubinary %t.fatbin -emit-llvm \
// RUN:            -target-sdk-version=12.3 %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG-HOST --input-file=%t.ll %s

// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fclangir \
// RUN:            -fcuda-is-device -emit-llvm -target-sdk-version=12.3 \
// RUN:            %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM-DEVICE --input-file=%t.ll %s

// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda \
// RUN:            -fcuda-is-device -emit-llvm -target-sdk-version=12.3 \
// RUN:            %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG-DEVICE --input-file=%t.ll %s

struct surfaceReference {
  int desc;
};

struct textureReference {
  int desc;
};

enum ReadMode {
  ElementType = 0,
  NormalizedFloat = 1
};

template <typename T, int dim = 1>
struct __attribute__((device_builtin_surface_type)) surface
    : public surfaceReference {};

template <typename T, int dim = 1, enum ReadMode mode = ElementType>
struct __attribute__((device_builtin_texture_type)) texture
    : public textureReference {};

surface<void, 2> surf;
texture<float, 2, NormalizedFloat> tex;

// On the host side both keep their original type and are internalized, and the
// runtime is told about them through the dedicated registration entry points.
// The surface/texture kind (2) and the texture's normalized flag (1) come from
// the template arguments.

// LLVM-HOST: @surf = internal global %"struct.surface<void, 2>" zeroinitializer, align 4
// LLVM-HOST: @tex = internal global %"struct.texture<float, 2, NormalizedFloat>" zeroinitializer, align 4
// LLVM-HOST: define internal void @__cuda_register_globals(ptr {{.*}}%[[HANDLE:[0-9]+]])
// LLVM-HOST:   call void @__cudaRegisterSurface(ptr %[[HANDLE]], ptr @surf, ptr @[[SURFNAME:[.a-zA-Z0-9_]+]], ptr @[[SURFNAME]], i32 2, i32 0)
// LLVM-HOST:   call void @__cudaRegisterTexture(ptr %[[HANDLE]], ptr @tex, ptr @[[TEXNAME:[.a-zA-Z0-9_]+]], ptr @[[TEXNAME]], i32 2, i32 1, i32 0)

// OGCG-HOST: @surf = internal global %struct.surface zeroinitializer, align 4
// OGCG-HOST: @tex = internal global %struct.texture zeroinitializer, align 4
// OGCG-HOST: define internal void @__cuda_register_globals(ptr {{.*}}%[[HANDLE:[0-9]+]])
// OGCG-HOST:   call void @__cudaRegisterSurface(ptr %[[HANDLE]], ptr @surf, ptr @[[SURFNAME:[.a-zA-Z0-9_]+]], ptr @[[SURFNAME]], i32 2, i32 0)
// OGCG-HOST:   call void @__cudaRegisterTexture(ptr %[[HANDLE]], ptr @tex, ptr @[[TEXNAME:[.a-zA-Z0-9_]+]], ptr @[[TEXNAME]], i32 2, i32 1, i32 0)

// On the device side both are object handles in a 64-bit integer.

// LLVM-DEVICE: @surf = addrspace(1) externally_initialized global i64 undef, align 4
// LLVM-DEVICE: @tex = addrspace(1) externally_initialized global i64 undef, align 4

// OGCG-DEVICE: @surf = addrspace(1) externally_initialized global i64 undef, align 4
// OGCG-DEVICE: @tex = addrspace(1) externally_initialized global i64 undef, align 4
