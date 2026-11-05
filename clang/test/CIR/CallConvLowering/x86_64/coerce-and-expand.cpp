// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG
//
// Tests for CoerceAndExpand ABI lowering on x86-64.
// Three-float struct → { <2 x float>, float }
// Four-float struct  → { <2 x float>, <2 x float> }

struct ThreeFloat { float a, b, c; };

// CIR-LABEL: define dso_local { <2 x float>, float } @_Z10make_threefff(
// CIR-SAME: float noundef %{{.*}}, float noundef %{{.*}}, float noundef %{{.*}})
// OGCG-LABEL: define dso_local { <2 x float>, float } @_Z10make_threefff(
// OGCG-SAME: float noundef %{{.*}}, float noundef %{{.*}}, float noundef %{{.*}})
ThreeFloat make_three(float x, float y, float z) {
  return {x, y, z};
}

// CIR-LABEL: define dso_local noundef float @_Z9sum_three10ThreeFloat(
// CIR-SAME: <2 x float> %{{.*}}, float %{{.*}})
// OGCG-LABEL: define dso_local noundef float @_Z9sum_three10ThreeFloat(
// OGCG-SAME: <2 x float> %{{.*}}, float %{{.*}})
float sum_three(ThreeFloat t) {
  return t.a + t.b + t.c;
}

// Round-trip: caller that receives CoerceAndExpand return and passes it as arg
// CIR-LABEL: define dso_local noundef float @_Z10round_tripfff(
// CIR: call { <2 x float>, float } @_Z10make_threefff(
// CIR: call noundef float @_Z9sum_three10ThreeFloat(<2 x float> %{{.*}}, float %{{.*}})
// OGCG-LABEL: define dso_local noundef float @_Z10round_tripfff(
// OGCG: call { <2 x float>, float } @_Z10make_threefff(
// OGCG: call noundef float @_Z9sum_three10ThreeFloat(<2 x float> %{{.*}}, float %{{.*}})
float round_trip(float x, float y, float z) {
  ThreeFloat t = make_three(x, y, z);
  return sum_three(t);
}

struct FourFloat { float a, b, c, d; };

// CIR-LABEL: define dso_local { <2 x float>, <2 x float> } @_Z9make_fourffff(
// OGCG-LABEL: define dso_local { <2 x float>, <2 x float> } @_Z9make_fourffff(
FourFloat make_four(float a, float b, float c, float d) {
  return {a, b, c, d};
}

// CIR-LABEL: define dso_local noundef float @_Z8sum_four9FourFloat(
// CIR-SAME: <2 x float> %{{.*}}, <2 x float> %{{.*}})
// OGCG-LABEL: define dso_local noundef float @_Z8sum_four9FourFloat(
// OGCG-SAME: <2 x float> %{{.*}}, <2 x float> %{{.*}})
float sum_four(FourFloat f) {
  return f.a + f.b + f.c + f.d;
}
