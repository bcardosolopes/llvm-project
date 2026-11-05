#!/bin/sh
# Like constituents-ld.sh, but lists only the .o files it was given: a linker
# that writes the merged pieces in some form the wrapper does not recognise.
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
  case $a in *.o) echo "$a" >> "$out" ;; esac
done
