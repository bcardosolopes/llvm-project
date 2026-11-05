#!/bin/sh
# Stands in for a cross-library optimizer's single-library link: lists every
# object and bitcode file it was given in the file --constituents-file names,
# as such a linker lists the inputs it took as bitcode.
out=
next=
for a in "$@"; do
  [ -n "$next" ] && { out=$a; next=; continue; }
  case $a in
    --constituents-file=*) out=${a#--constituents-file=} ;;
    --constituents-file) next=1 ;;
  esac
done
: > "$out"
for a in "$@"; do
  case $a in *.o | *.bc | *.bc.*) echo "$a" >> "$out" ;; esac
done
