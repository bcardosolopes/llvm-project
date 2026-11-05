// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -mconstructor-aliases -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

typedef __builtin_va_list va_list;

static __inline__ __attribute__((__always_inline__)) __attribute__((__format__(printf, 3, 0)))
int vsnprintf(char* const __attribute__((pass_object_size(1))) dest, int size, const char* format, va_list ap)
        __attribute__((overloadable)) {
    return __builtin___vsnprintf_chk(dest, size, 0, __builtin_object_size(((dest)), (1)), format, ap);
}

typedef long unsigned int size_t;

size_t __strlen_chk(const char* __s, size_t __n) __attribute__((annotate("introduced_in=" "17")));
size_t strlen(const char* __s) __attribute__((__pure__));
static __inline__ __attribute__((__always_inline__))
size_t strlen(const char* const s __attribute__((pass_object_size(0)))) __attribute__((overloadable)) {
    size_t bos = __builtin_object_size(((s)), (0));

    if (bos == ((size_t) -1)) {
        return __builtin_strlen(s);
    }

    return __strlen_chk(s, bos);
}

void log(int, const char *, int);

void consume_message(const char *m) {
  log(3, m, strlen(m));
}

void t(const char* fmt, ...) {
  va_list args;
  __builtin_va_start(args, fmt);
  const int size = 512;
  char message[size];
  vsnprintf(message, size, fmt, args);
  consume_message(message);
}

// CHECK: cir.func {{.*}} @_Z15consume_messagePKc(%arg0: !cir.ptr<!s8i>
// CHECK:   %0 = cir.alloca !cir.ptr<!s8i>, !cir.ptr<!cir.ptr<!s8i>>, ["m", init] {alignment = 8 : i64}

// CHECK:   %3 = cir.load{{.*}} %0 : !cir.ptr<!cir.ptr<!s8i>>, !cir.ptr<!s8i>
// CHECK:   %4 = cir.objsize max nullunknown %3 : !cir.ptr<!s8i> -> !u64i
// CHECK:   %5 = cir.call @_ZL6strlenPKcU17pass_object_size0(%3, %4) : (!cir.ptr<!s8i>, !u64i) -> !u64i

// CHECK: cir.func {{.*}} @_ZL9vsnprintfPcU17pass_object_size1iPKcP13__va_list_tag

// Implicit size parameter in arg %arg1
//
// FIXME: tag the param with an attribute to designate the size information.
//
// CHECK: %[[DEST:.*]] = cir.alloca !cir.ptr<!s8i>, !cir.ptr<!cir.ptr<!s8i>>, ["dest", init, const] {alignment = 8 : i64}
// CHECK: %[[SZ:.*]] = cir.alloca !u64i, !cir.ptr<!u64i>, ["", init] {alignment = 8 : i64}

// CHECK: cir.store %arg0, %[[DEST]] : !cir.ptr<!s8i>, !cir.ptr<!cir.ptr<!s8i>>
// CHECK: cir.store %arg1, %[[SZ]] : !u64i, !cir.ptr<!u64i>

// The callee uses the hidden pass_object_size parameter directly instead of
// emitting @cir.objsize.
// CHECK: cir.load align(8) %[[SZ]] : !cir.ptr<!u64i>, !u64i
// CHECK: cir.call @__vsnprintf_chk(
// CHECK: cir.func {{.*}} @__vsnprintf_chk
