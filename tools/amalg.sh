#!/bin/sh
# Inline the parts of src/apl.c into one self-contained C file (stdout).
# Usage: sh tools/amalg.sh > apl_all.c
cd "$(dirname "$0")/../src" || exit 1
awk '
/^#include "p[0-9][b-d]?_[a-z]+\.c"$/ {
  f = $2; gsub(/"/, "", f);
  print "/* ===== " f " ===== */";
  while ((getline line < f) > 0) print line;
  close(f);
  next
}
{ print }' apl.c
