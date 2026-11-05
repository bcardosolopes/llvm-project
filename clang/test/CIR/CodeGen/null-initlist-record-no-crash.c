// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -fclangir-mem2reg -emit-obj -o /dev/null %s

// Reduced from a production crash where nested aggregate init-lists for
// globals required null-filling of an uninitialized record field.
// This used to crash while materializing a temporary cir.const in
// ConstantEmitter::emitNullForMemory.

typedef union {
  long __private[5];
} pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER { {0, 0, 0, 0, 0} }

enum EngineKind { UNKNOWN, DIESEL, ELECTRIC };
enum BrakeKind { NONE, DISC, DRUM, REGEN };

static struct {
  enum EngineKind engine_kind;
  union {
    struct {
      enum BrakeKind brakeKind;
      union {
        struct {
          void (*discBrakeOn)();
          void (*discBrakeOff)();
        } disc;
        struct {
          void *(*regenStart)(void *, const char *, _Bool);
          void (*regenStop)(void *);
          void *regenState;
        } regen;
      } brakes;
    } electric;
    struct {
      void (*dieselIdleAll)(int gear);
      void (*dieselRevAll)(int gear);
    } diesel;
  } engine;
  _Bool initialized;
  pthread_mutex_t mutex;
} garage_globals = {
    UNKNOWN,
    {},
    0,
    PTHREAD_MUTEX_INITIALIZER,
};

int main(void) { return garage_globals.initialized; }
