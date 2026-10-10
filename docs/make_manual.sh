#!/bin/sh
# Builds docs/APL-User-Guide.docx (twice, so the contents have real page numbers).
# Needs: node with the docx package, LibreOffice (soffice) and poppler (pdftotext).
set -e
cd "$(dirname "$0")/.."
[ -x ./apl ] || make
rm -f docs/pages.json
node docs/build_manual.js
for pass in 1 2; do
  (cd docs && soffice --headless --convert-to pdf APL-User-Guide.docx >/dev/null 2>&1)
  node docs/pages.js docs/APL-User-Guide.pdf docs/_toc.json docs/pages.json
  node docs/build_manual.js docs/pages.json
done
(cd docs && soffice --headless --convert-to pdf APL-User-Guide.docx >/dev/null 2>&1)
echo "built docs/APL-User-Guide.docx"
