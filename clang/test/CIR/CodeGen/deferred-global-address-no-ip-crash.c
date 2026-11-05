// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -fclangir -fclangir-mem2reg -emit-obj -o /dev/null %s

typedef __UINTPTR_TYPE__ uint64_t;

typedef struct planet_ring_t planet_ring_t;
typedef struct {
  unsigned long flags;
  planet_ring_t *ring;
} planet_extinfo;

typedef void *(*make_ring_fn)(const char *, const char *,
                                               const char *, uint64_t,
                                               const char *,
                                               planet_ring_t *);
typedef void *(*open_ring_fn)(const char *, int,
                                         const planet_extinfo *);

extern void *load_lib(const char *, int);
extern void on_open_error(int);
extern void on_moon_error(int, const char *);
extern void *dlsym(void *, const char *);

static planet_ring_t *jupiter_ring;
static make_ring_fn make_ring;
static open_ring_fn open_ring;

static int setup_ring(void) {
  void *saturn_handle = load_lib("libsaturn.so", 2);
  if (!saturn_handle) {
    on_open_error(201);
    return 0;
  }

  make_ring = (make_ring_fn)dlsym(
      saturn_handle, "make_ring");
  if (!make_ring) {
    on_open_error(203);
    return 0;
  }

  void *venus_handle = load_lib("libvenus.so", 2);
  if (!venus_handle) {
    on_open_error(202);
    return 0;
  }

  open_ring = (open_ring_fn)dlsym(venus_handle,
                                                      "open_ring");
  if (!open_ring) {
    on_open_error(203);
    return 0;
  }

  jupiter_ring = make_ring("jupiter", "orbit/", "orbit/", 3,
                                         "orbit/", (void *)0);
  if (!jupiter_ring) {
    on_moon_error(204, "Failed to create ring");
    return 0;
  }
  return 1;
}

void *open_planet(const char *path, int mode) {
  if (!setup_ring())
    return (void *)0;

  const planet_extinfo extinfo = {
      .flags = 512,
      .ring = jupiter_ring,
  };

  return open_ring(path, mode, &extinfo);
}
