// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-clangir-call-conv-lowering -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-clangir-call-conv-lowering -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Test that _BitInt types larger than 128 bits are properly supported.

// Test function parameter and return with large signed _BitInt.
_BitInt(256) test_large_signed(_BitInt(256) a) {
  return a;
}

// CIR-LABEL: @_Z17test_large_signedDB256_
// CIR:         %{{.+}} = cir.load {{.*}} !cir.ptr<!s256i>, !s256i
// CIR:       }

// LLVM-LABEL: @_Z17test_large_signedDB256_
// LLVM:         %{{.+}} = load i256
// LLVM:       }

// Test function parameter and return with large unsigned _BitInt.
unsigned _BitInt(129) test_large_unsigned(unsigned _BitInt(129) x) {
  return x;
}

// CIR-LABEL: @_Z19test_large_unsignedDU129_
// CIR:         %{{.+}} = cir.load {{.*}} !cir.ptr<!cir.int<u, 129>>, !cir.int<u, 129>
// CIR:       }

// LLVM-LABEL: @_Z19test_large_unsignedDU129_
// LLVM:         %{{.+}} = load i192
// LLVM:         %{{.+}} = trunc i192 %{{.+}} to i129
// LLVM:       }

// Test arithmetic on large _BitInt.
_BitInt(256) test_large_arith(_BitInt(256) a, _BitInt(256) b) {
  return a + b;
}

// CIR-LABEL: @_Z16test_large_arithDB256_S_
// CIR:         %[[A:.*]] = cir.load {{.*}} !cir.ptr<!s256i>, !s256i
// CIR:         %[[B:.*]] = cir.load {{.*}} !cir.ptr<!s256i>, !s256i
// CIR:         %{{.+}} = cir.binop(add, %[[A]], %[[B]]) nsw : !s256i
// CIR:       }

// LLVM-LABEL: @_Z16test_large_arithDB256_S_
// LLVM:         %{{.+}} = add nsw i256
// LLVM:       }
