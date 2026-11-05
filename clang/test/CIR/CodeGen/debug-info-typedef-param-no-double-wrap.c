// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -fclangir -fclangir-mem2reg \
// RUN:   -emit-llvm -debug-info-kind=constructor -dwarf-version=4 -gkey-instructions \
// RUN:   %s -o %t.ll -w
// RUN: FileCheck --input-file=%t.ll %s

// Regression test: When multiple functions share the same typedef
// parameter type (e.g. const Byte *), the debug info type rewriter must not
// create double-wrapped typedef DI types. Previously, replaceRewrittenTypes
// would substitute the base type (e.g. unsigned char) inside an existing
// typedef wrapper with a typedef-wrapped version from another parameter's
// rewrite, creating Byte(Byte(unsigned char)) instead of Byte(unsigned char).
// The LLVM verifier rejects this as "conflicting debug info for argument".

typedef unsigned char Byte;
typedef unsigned long SizeT;

// This function has Byte*/const Byte * parameters whose debug types start
// without the Byte typedef wrapper. rewriteRecordType adds it, and the
// rewrite must NOT leak into other parameters' type trees via
// replaceRewrittenTypes.
static int inner(void *pp,
    Byte *dest, SizeT *destLen,
    const Byte *src, SizeT *srcLen, int srcWasFinished,
    int finishMode)
{
  SizeT destRem = *destLen;
  SizeT srcRem = *srcLen;
  *destLen = 0;
  *srcLen = 0;
  while (destRem > 0 && srcRem > 0) {
    *dest++ = *src++;
    destRem--;
    srcRem--;
  }
  *destLen = destRem;
  *srcLen = srcRem;
  return 0;
}

// This function also has const Byte * at arg 4 and complex control flow
// that produces phi nodes merging the src pointer. The phi-derived
// #dbg_value records must use the SAME DILocalVariable as the initial one.
int outer(void *p, Byte *dest, SizeT *destLen,
    const Byte *src, SizeT *srcLen, int srcFinished,
    int finishMode)
{
  SizeT destLenOrig = *destLen;
  SizeT srcLenOrig = *srcLen;
  *destLen = 0;
  *srcLen = 0;

  for (;;) {
    SizeT srcRem;

    if (*(int*)p == 1) {
      SizeT destLen2 = destLenOrig - *destLen;
      SizeT srcLen2 = srcLenOrig - *srcLen;
      int res = inner(p, dest + *destLen, &destLen2,
          src + *srcLen, &srcLen2, srcFinished, finishMode);
      *destLen += destLen2;
      *srcLen += srcLen2;
      if (res != 0) return res;
    }

    srcRem = srcLenOrig - *srcLen;
    if (srcRem == 0) return 0;

    if (*(int*)p == 2) {
      src++;
      (*srcLen)++;
      continue;
    }
    if (*(int*)p == 3) {
      src++;
      (*srcLen)++;
      *(int*)p = 0;
      continue;
    }
    if (*(int*)p == 0) {
      src++;
      (*srcLen)++;
    }
  }
}

// Verify that the outer function has exactly one DILocalVariable for 'src'
// at arg 4, and that all #dbg_value records for 'src' use the same variable.
// CHECK: define {{.*}} @outer(
// CHECK: ![[OUTER_SP:[0-9]+]] = distinct !DISubprogram(name: "outer"
// CHECK: ![[SRC_VAR:[0-9]+]] = !DILocalVariable(name: "src", arg: 4, scope: ![[OUTER_SP]]
// CHECK-NOT: !DILocalVariable(name: "src", arg: 4, scope: ![[OUTER_SP]]
