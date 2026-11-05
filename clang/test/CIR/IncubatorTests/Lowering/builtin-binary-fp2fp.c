// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fmath-errno -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffast-math -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM-FASTMATH
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffast-math -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// copysign

float my_copysignf(float x, float y) {
  return __builtin_copysignf(x, y);
}

// LLVM: define dso_local float @my_copysignf
// LLVM:   %{{.+}} = call float @llvm.copysign.f32(float %{{.+}}, float %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) float @my_copysignf
// LLVM-FASTMATH:   %{{.+}} = call reassoc nnan ninf nsz arcp afn float @llvm.copysign.f32(float %{{.+}}, float %{{.+}})
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) float @my_copysignf
// OGCG:   %{{.+}} = call reassoc nnan ninf nsz arcp afn float @llvm.copysign.f32(float %{{.+}}, float %{{.+}})
// OGCG: }

double my_copysign(double x, double y) {
  return __builtin_copysign(x, y);
}

// LLVM: define dso_local double @my_copysign
// LLVM:   %{{.+}} = call double @llvm.copysign.f64(double %{{.+}}, double %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) double @my_copysign
// LLVM-FASTMATH:   %{{.+}} = call reassoc nnan ninf nsz arcp afn double @llvm.copysign.f64(double %{{.+}}, double %{{.+}})
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) double @my_copysign
// OGCG:   %{{.+}} = call reassoc nnan ninf nsz arcp afn double @llvm.copysign.f64(double %{{.+}}, double %{{.+}})
// OGCG: }

long double my_copysignl(long double x, long double y) {
  return __builtin_copysignl(x, y);
}

// LLVM: define dso_local x86_fp80 @my_copysignl
// LLVM:   %{{.+}} = call x86_fp80 @llvm.copysign.f80(x86_fp80 %{{.+}}, x86_fp80 %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) x86_fp80 @my_copysignl
// LLVM-FASTMATH:   %{{.+}} = call reassoc nnan ninf nsz arcp afn x86_fp80 @llvm.copysign.f80(x86_fp80 %{{.+}}, x86_fp80 %{{.+}})
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) x86_fp80 @my_copysignl
// OGCG:   %{{.+}} = call reassoc nnan ninf nsz arcp afn x86_fp80 @llvm.copysign.f80(x86_fp80 %{{.+}}, x86_fp80 %{{.+}})
// OGCG: }

// fmax

float my_fmaxf(float x, float y) {
  return __builtin_fmaxf(x, y);
}

// LLVM: define dso_local float @my_fmaxf
// LLVM:   %{{.+}} = call float @llvm.maxnum.f32(float %{{.+}}, float %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) float @my_fmaxf
// LLVM-FASTMATH:   %{{.+}} = call reassoc nnan ninf nsz arcp afn float @llvm.maxnum.f32(float %{{.+}}, float %{{.+}})
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) float @my_fmaxf
// OGCG:   %{{.+}} = call reassoc nnan ninf nsz arcp afn float @llvm.maxnum.f32(float %{{.+}}, float %{{.+}})
// OGCG: }

double my_fmax(double x, double y) {
  return __builtin_fmax(x, y);
}

// LLVM: define dso_local double @my_fmax
// LLVM:   %{{.+}} = call double @llvm.maxnum.f64(double %{{.+}}, double %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) double @my_fmax
// LLVM-FASTMATH:   %{{.+}} = call reassoc nnan ninf nsz arcp afn double @llvm.maxnum.f64(double %{{.+}}, double %{{.+}})
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) double @my_fmax
// OGCG:   %{{.+}} = call reassoc nnan ninf nsz arcp afn double @llvm.maxnum.f64(double %{{.+}}, double %{{.+}})
// OGCG: }

long double my_fmaxl(long double x, long double y) {
  return __builtin_fmaxl(x, y);
}

// LLVM: define dso_local x86_fp80 @my_fmaxl
// LLVM:   %{{.+}} = call x86_fp80 @llvm.maxnum.f80(x86_fp80 %{{.+}}, x86_fp80 %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) x86_fp80 @my_fmaxl
// LLVM-FASTMATH:   %{{.+}} = call reassoc nnan ninf nsz arcp afn x86_fp80 @llvm.maxnum.f80(x86_fp80 %{{.+}}, x86_fp80 %{{.+}})
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) x86_fp80 @my_fmaxl
// OGCG:   %{{.+}} = call reassoc nnan ninf nsz arcp afn x86_fp80 @llvm.maxnum.f80(x86_fp80 %{{.+}}, x86_fp80 %{{.+}})
// OGCG: }

// fmin

float my_fminf(float x, float y) {
  return __builtin_fminf(x, y);
}

// LLVM: define dso_local float @my_fminf
// LLVM:   %{{.+}} = call float @llvm.minnum.f32(float %{{.+}}, float %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) float @my_fminf
// LLVM-FASTMATH:   %{{.+}} = call reassoc nnan ninf nsz arcp afn float @llvm.minnum.f32(float %{{.+}}, float %{{.+}})
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) float @my_fminf
// OGCG:   %{{.+}} = call reassoc nnan ninf nsz arcp afn float @llvm.minnum.f32(float %{{.+}}, float %{{.+}})
// OGCG: }

double my_fmin(double x, double y) {
  return __builtin_fmin(x, y);
}

// LLVM: define dso_local double @my_fmin
// LLVM:   %{{.+}} = call double @llvm.minnum.f64(double %{{.+}}, double %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) double @my_fmin
// LLVM-FASTMATH:   %{{.+}} = call reassoc nnan ninf nsz arcp afn double @llvm.minnum.f64(double %{{.+}}, double %{{.+}})
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) double @my_fmin
// OGCG:   %{{.+}} = call reassoc nnan ninf nsz arcp afn double @llvm.minnum.f64(double %{{.+}}, double %{{.+}})
// OGCG: }

long double my_fminl(long double x, long double y) {
  return __builtin_fminl(x, y);
}

// LLVM: define dso_local x86_fp80 @my_fminl
// LLVM:   %{{.+}} = call x86_fp80 @llvm.minnum.f80(x86_fp80 %{{.+}}, x86_fp80 %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) x86_fp80 @my_fminl
// LLVM-FASTMATH:   %{{.+}} = call reassoc nnan ninf nsz arcp afn x86_fp80 @llvm.minnum.f80(x86_fp80 %{{.+}}, x86_fp80 %{{.+}})
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) x86_fp80 @my_fminl
// OGCG:   %{{.+}} = call reassoc nnan ninf nsz arcp afn x86_fp80 @llvm.minnum.f80(x86_fp80 %{{.+}}, x86_fp80 %{{.+}})
// OGCG: }

// fmod

float my_fmodf(float x, float y) {
  return __builtin_fmodf(x, y);
}

// LLVM: define dso_local float @my_fmodf
// LLVM:   %{{.+}} = call float @fmodf(float noundef %{{.+}}, float noundef %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) float @my_fmodf
// LLVM-FASTMATH:   %{{.+}} = frem reassoc nnan ninf nsz arcp afn float %{{.+}}, %{{.+}}
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) float @my_fmodf
// OGCG:   %{{.+}} = frem reassoc nnan ninf nsz arcp afn float %{{.+}}, %{{.+}}
// OGCG: }

double my_fmod(double x, double y) {
  return __builtin_fmod(x, y);
}

// LLVM: define dso_local double @my_fmod
// LLVM:   %{{.+}} = call double @fmod(double noundef %{{.+}}, double noundef %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) double @my_fmod
// LLVM-FASTMATH:   %{{.+}} = frem reassoc nnan ninf nsz arcp afn double %{{.+}}, %{{.+}}
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) double @my_fmod
// OGCG:   %{{.+}} = frem reassoc nnan ninf nsz arcp afn double %{{.+}}, %{{.+}}
// OGCG: }

long double my_fmodl(long double x, long double y) {
  return __builtin_fmodl(x, y);
}

// LLVM: define dso_local x86_fp80 @my_fmodl
// LLVM:   %{{.+}} = call x86_fp80 @fmodl(x86_fp80 noundef %{{.+}}, x86_fp80 noundef %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) x86_fp80 @my_fmodl
// LLVM-FASTMATH:   %{{.+}} = frem reassoc nnan ninf nsz arcp afn x86_fp80 %{{.+}}, %{{.+}}
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) x86_fp80 @my_fmodl
// OGCG:   %{{.+}} = frem reassoc nnan ninf nsz arcp afn x86_fp80 %{{.+}}, %{{.+}}
// OGCG: }

// pow

float my_powf(float x, float y) {
  return __builtin_powf(x, y);
}

// LLVM: define dso_local float @my_powf
// LLVM:   %{{.+}} = call float @powf(float noundef %{{.+}}, float noundef %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) float @my_powf
// LLVM-FASTMATH:   %{{.+}} = call reassoc nnan ninf nsz arcp afn float @llvm.pow.f32(float %{{.+}}, float %{{.+}})
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) float @my_powf
// OGCG:   %{{.+}} = call reassoc nnan ninf nsz arcp afn float @llvm.pow.f32(float %{{.+}}, float %{{.+}})
// OGCG: }

double my_pow(double x, double y) {
  return __builtin_pow(x, y);
}

// LLVM: define dso_local double @my_pow
// LLVM:   %{{.+}} = call double @pow(double noundef %{{.+}}, double noundef %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) double @my_pow
// LLVM-FASTMATH:   %{{.+}} = call reassoc nnan ninf nsz arcp afn double @llvm.pow.f64(double %{{.+}}, double %{{.+}})
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) double @my_pow
// OGCG:   %{{.+}} = call reassoc nnan ninf nsz arcp afn double @llvm.pow.f64(double %{{.+}}, double %{{.+}})
// OGCG: }

long double my_powl(long double x, long double y) {
  return __builtin_powl(x, y);
}

// LLVM: define dso_local x86_fp80 @my_powl
// LLVM:   %{{.+}} = call x86_fp80 @powl(x86_fp80 noundef %{{.+}}, x86_fp80 noundef %{{.+}})
// LLVM: }

// LLVM-FASTMATH: define dso_local nofpclass(nan inf) x86_fp80 @my_powl
// LLVM-FASTMATH:   %{{.+}} = call reassoc nnan ninf nsz arcp afn x86_fp80 @llvm.pow.f80(x86_fp80 %{{.+}}, x86_fp80 %{{.+}})
// LLVM-FASTMATH: }

// OGCG: define dso_local nofpclass(nan inf) x86_fp80 @my_powl
// OGCG:   %{{.+}} = call reassoc nnan ninf nsz arcp afn x86_fp80 @llvm.pow.f80(x86_fp80 %{{.+}}, x86_fp80 %{{.+}})
// OGCG: }

