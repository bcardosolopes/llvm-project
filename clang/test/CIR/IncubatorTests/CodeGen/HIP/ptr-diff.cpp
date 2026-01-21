#include "cuda.h"

// RUN: %clang_cc1 -triple=amdgcn-amd-amdhsa -x hip -fclangir \
// RUN:            -fcuda-is-device -fhip-new-launch-api \
// RUN:            -I%S/../Inputs/ -emit-cir %s -o %t.ll
// RUN: FileCheck --check-prefix=CIR-DEVICE --input-file=%t.ll %s

// RUN: %clang_cc1 -triple=amdgcn-amd-amdhsa -x hip -fclangir \
// RUN:            -fcuda-is-device -fhip-new-launch-api \
// RUN:            -I%S/../Inputs/ -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM-DEVICE --input-file=%t.ll %s

// RUN: %clang_cc1 -triple=amdgcn-amd-amdhsa -x hip  \
// RUN:            -fcuda-is-device -fhip-new-launch-api \
// RUN:            -I%S/../Inputs/ -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG-DEVICE --input-file=%t.ll %s

__device__ int ptr_diff() {
  const char c_str[] = "c-string"; 
  const char* len =  c_str;  
  return c_str - len;
}


// CIR-DEVICE: %[[#CStrAlloca:]] = cir.alloca !cir.array<!s8i x 9>, !cir.ptr<!cir.array<!s8i x 9>, lang_address_space(offload_private)>, ["c_str", init, const]
// CIR-DEVICE: %[[#LenLocalAlloca:]] = cir.alloca !cir.ptr<!s8i>, !cir.ptr<!cir.ptr<!s8i>, lang_address_space(offload_private)>, ["len", init]
// CIR-DEVICE: %[[#CStrAddrCast:]] = cir.cast address_space %[[#CStrAlloca]] : !cir.ptr<!cir.array<!s8i x 9>, lang_address_space(offload_private)> -> !cir.ptr<!cir.array<!s8i x 9>>
// CIR-DEVICE: %[[#GlobalCStr:]] = cir.get_global @__const._Z8ptr_diffv.c_str : !cir.ptr<!cir.array<!s8i x 9>, target_address_space(4)>
// CIR-DEVICE: %[[#GlobalCStrCast:]] = cir.cast address_space %[[#GlobalCStr]] : !cir.ptr<!cir.array<!s8i x 9>, target_address_space(4)> -> !cir.ptr<!cir.array<!s8i x 9>>
// CIR-DEVICE: cir.copy %[[#GlobalCStrCast]] to %[[#CStrAddrCast]] : !cir.ptr<!cir.array<!s8i x 9>>
// CIR-DEVICE: %[[#LenLocalAddr:]] = cir.cast address_space %[[#LenLocalAlloca]] : !cir.ptr<!cir.ptr<!s8i>, lang_address_space(offload_private)> -> !cir.ptr<!cir.ptr<!s8i>>
// CIR-DEVICE: %[[#Decayed1:]] = cir.cast array_to_ptrdecay %[[#CStrAddrCast]] : !cir.ptr<!cir.array<!s8i x 9>> -> !cir.ptr<!s8i>
// CIR-DEVICE: cir.store align(8) %[[#Decayed1]], %[[#LenLocalAddr]] : !cir.ptr<!s8i>, !cir.ptr<!cir.ptr<!s8i>>
// CIR-DEVICE: %[[#Decayed2:]] = cir.cast array_to_ptrdecay %[[#CStrAddrCast]] : !cir.ptr<!cir.array<!s8i x 9>> -> !cir.ptr<!s8i>
// CIR-DEVICE: %[[#LoadedLen:]] = cir.load align(8) %[[#LenLocalAddr]] : !cir.ptr<!cir.ptr<!s8i>>, !cir.ptr<!s8i>
// CIR-DEVICE: cir.ptr_diff %[[#Decayed2]], %[[#LoadedLen]] : !cir.ptr<!s8i>

// LLVM-DEVICE: define dso_local i32 @_Z8ptr_diffv()
// LLVM-DEVICE: alloca i32, i64 1, align 4, addrspace(5)
// LLVM-DEVICE: %[[#CStrAlloca:]] = alloca [9 x i8], i64 1, align 1, addrspace(5)
// LLVM-DEVICE: %[[#LenLocalAddr:]] = alloca ptr, i64 1, align 8, addrspace(5)
// LLVM-DEVICE: %[[#CStrCast:]] = addrspacecast ptr addrspace(5) %[[#CStrAlloca]] to ptr
// LLVM-DEVICE: call void @llvm.memcpy.p0.p0.i64(ptr %[[#CStrCast]], ptr addrspacecast (ptr addrspace(4) @__const._Z8ptr_diffv.c_str to ptr), i64 9, i1 false)
// LLVM-DEVICE: %[[#LenLocalAddrCast:]] = addrspacecast ptr addrspace(5) %[[#LenLocalAddr]] to ptr
// LLVM-DEVICE: %[[#GEP1:]] = getelementptr i8, ptr %[[#CStrCast]], i32 0
// LLVM-DEVICE: store ptr %[[#GEP1]], ptr %[[#LenLocalAddrCast]], align 8
// LLVM-DEVICE: %[[#GEP2:]] = getelementptr i8, ptr %[[#CStrCast]], i32 0
// LLVM-DEVICE: %[[#LoadedLen:]] = load ptr, ptr %[[#LenLocalAddrCast]], align 8
// LLVM-DEVICE: %[[#LHS:]] = ptrtoint ptr %[[#GEP2]] to i64
// LLVM-DEVICE: %[[#RHS:]] = ptrtoint ptr %[[#LoadedLen]] to i64
// LLVM-DEVICE: sub i64 %[[#LHS]], %[[#RHS]]

// OGCG-DEVICE: define dso_local noundef i32 @_Z8ptr_diffv() #0
// OGCG-DEVICE: %[[RETVAL:.*]] = alloca i32, align 4, addrspace(5)
// OGCG-DEVICE: %[[C_STR:.*]] = alloca [9 x i8], align 1, addrspace(5)
// OGCG-DEVICE: %[[LEN:.*]] = alloca ptr, align 8, addrspace(5)
// OGCG-DEVICE: %[[RETVAL_ASCAST:.*]] = addrspacecast ptr addrspace(5) %[[RETVAL]] to ptr
// OGCG-DEVICE: %[[C_STR_ASCAST:.*]] = addrspacecast ptr addrspace(5) %[[C_STR]] to ptr
// OGCG-DEVICE: %[[LEN_ASCAST:.*]] = addrspacecast ptr addrspace(5) %[[LEN]] to ptr
// OGCG-DEVICE: %[[ARRAYDECAY:.*]] = getelementptr inbounds [9 x i8], ptr %[[C_STR_ASCAST]], i64 0, i64 0
// OGCG-DEVICE: store ptr %[[ARRAYDECAY]], ptr %[[LEN_ASCAST]], align 8
// OGCG-DEVICE: %[[ARRAYDECAY1:.*]] = getelementptr inbounds [9 x i8], ptr %[[C_STR_ASCAST]], i64 0, i64 0
// OGCG-DEVICE: %[[LOADED:.*]] = load ptr, ptr %[[LEN_ASCAST]], align 8
// OGCG-DEVICE: %[[LHS:.*]] = ptrtoint ptr %[[ARRAYDECAY1]] to i64
// OGCG-DEVICE: %[[RHS:.*]] = ptrtoint ptr %[[LOADED]] to i64
// OGCG-DEVICE: %[[SUB:.*]] = sub i64 %[[LHS]], %[[RHS]]
// OGCG-DEVICE: %[[CONV:.*]] = trunc i64 %[[SUB]] to i32
