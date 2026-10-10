// pages.js - reads the PDF of the manual and writes the page number of every
// contents entry to pages.json (page numbering starts at the Preface)
const cp = require('child_process'), fs = require('fs');
const pdf = process.argv[2], tocf = process.argv[3], out = process.argv[4];
const toc = JSON.parse(fs.readFileSync(tocf, 'utf8'));
const n = parseInt(/Pages:\s+(\d+)/.exec(cp.execSync('pdfinfo "' + pdf + '"').toString())[1], 10);
const pages = [];
for (let i = 1; i <= n; i++) pages.push(cp.execSync('pdftotext -f ' + i + ' -l ' + i + ' -layout "' + pdf + '" -').toString().split('\n').map(l => l.trim()));
// first body page: the first page after the contents that has the Preface heading and the running header
let start = -1;
for (let i = 0; i < n; i++) if (pages[i].includes('Preface') && !pages[i].includes('Contents') && pages[i].some(l => l.startsWith('APL Introduction'))) { start = i; break; }
if (start < 0) throw new Error('body start not found');
const res = {};
let from = start;
for (let ti = 0; ti < toc.length; ti++) {
  const e = toc[ti];
  let found = -1;
  for (let i = from; i < n; i++) if (pages[i].includes(e.text)) { found = i; break; }
  if (found < 0) throw new Error('heading not found: ' + e.text);
  res[ti] = found - start + 1;
  from = found;
}
fs.writeFileSync(out, JSON.stringify(res));
console.log('body starts on pdf page', start + 1, 'of', n);
