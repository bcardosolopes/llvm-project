// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O3 -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM-NOOPT
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O3 %s -o - | FileCheck %s --check-prefix=LLVM-O3

// Regression test for CIR codegen aggregate-return pessimism on small structs
// CIR codegen emits one cir.return per return statement, each loading
// from the shared __retval alloca. Without the cir-flatten-cfg pass's
// unifyRetvalMultiReturns step, multiple aggregate loads of __retval prevent
// SROA from recursively splitting nested struct/union types — defeating the
// SROA-friendly shape that classic codegen produces (single ReturnBlock +
// EmitBranchThroughCleanup) and pessimizing real workloads (DuckDB
// hashjoin_dups_rhs +9% regression on string_t-returning callees).
//
// This test ensures:
//   (a) at -O3 -disable-llvm-passes, the CIR-emitted IR has a single ret
//       fed by a load from the shared __retval slot (the unification fired);
//   (b) at -O3, the resulting LLVM IR splits the 16-byte string_t-like struct
//       into the SROA-friendly [12 x i8] align 4 trailing payload (matching
//       what classic codegen emits for the same source shape).

// Avoid <cstdint> so the test runs without stdlib headers.
typedef unsigned int uint32_t;

struct str_t {
    union {
        struct { uint32_t length; char prefix[4]; char *ptr; } pointer;
        struct { uint32_t length; char inlined[12]; } inlined;
    } value;

    str_t() {}
    str_t(const char *data, uint32_t len) {
        value.inlined.length = len;
        if (len <= 12) {
            __builtin_memset(value.inlined.inlined, 0, 12);
            if (len == 0) return;
            __builtin_memcpy(value.inlined.inlined, data, len);
        } else {
            __builtin_memcpy(value.pointer.prefix, data, 4);
            value.pointer.ptr = const_cast<char *>(data);
        }
    }
};

extern "C" str_t read_overflow(int x);

inline str_t fetch(const char *data, uint32_t len, int branch) {
    if (__builtin_expect(branch >= 0, 1))
        return str_t(data, len);
    else
        return read_overflow(branch);
}

extern "C" void scan(str_t *out, const char *base, const unsigned *offsets, unsigned n) {
    for (unsigned i = 0; i < n; i++) {
        unsigned off = offsets[i];
        unsigned len = offsets[i + 1] - off;
        out[i] = fetch(base + off, len, (int)len);
    }
}

// Property (a): pre-LLVM-passes, fetch() has exactly one ret instruction. The
// two source-level returns are unified via cir-flatten-cfg's
// unifyRetvalMultiReturns into a single load+ret block.
//
// LLVM-NOOPT-LABEL: define {{.*}} @_Z5fetchPKcji(
// LLVM-NOOPT:       alloca %struct.str_t,
// LLVM-NOOPT-NOT:   ret { i64, ptr }
// LLVM-NOOPT:       load { i64, ptr }, ptr
// LLVM-NOOPT:       ret { i64, ptr }
// LLVM-NOOPT-NOT:   ret { i64, ptr }

// Property (b): post-O3, scan()'s inlined string_t alloca is the SROA-friendly
// [12 x i8] align 4 trailing-payload shape (the leading uint32_t is split into
// an SSA value), matching classic codegen. Crucially, no insertvalue / phi
// over { i64, ptr } in the inner loop body — those are the symptom of SROA
// being unable to recursively split when each return path loads __retval as a
// whole aggregate.
//
// LLVM-O3-LABEL: define {{.*}} @scan(
// LLVM-O3:       alloca [12 x i8], align 4
// LLVM-O3-NOT:   alloca %struct.anon{{.*}}, align 8
// LLVM-O3-NOT:   insertvalue { i64, ptr }
// LLVM-O3-NOT:   phi { i64, ptr }
