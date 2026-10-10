// build_manual.js - generates APL-User-Guide.docx
// usage: node docs/build_manual.js [pages.json]   (run from the repository root)
// Figures marked "run" execute ./apl, so every printed result is real.
const fs = require('fs');
const path = require('path');
const cp = require('child_process');
const {
  Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell, WidthType,
  ShadingType, BorderStyle, AlignmentType, LevelFormat, Footer, Header,
  PageNumber, TabStopType, PageBreak, SectionType, HeadingLevel
} = require('docx');

const ROOT = process.cwd();
const APL = path.join(ROOT, 'apl');
const OUT = process.argv[3] || path.join(ROOT, 'docs', 'APL-User-Guide.docx');
const PAGES = fs.existsSync(process.argv[2] || '') ? JSON.parse(fs.readFileSync(process.argv[2], 'utf8')) : {};
const PUB = 'APL-0001-0';
const HDR = "APL Introduction and User\u2019s Guide";
const CONTENT_W = 9638;            // A4 minus 1134 margins both sides
const SANS = 'Arial', SERIF = 'Times New Roman', MONO = 'Courier New', SYM = 'Segoe UI Symbol';

// ------------------------------------------------------------ inline markup
// `code`  **bold**  (nothing else)
function runs(text, o = {}) {
  const out = [];
  const size = o.size || 21, font = o.font || SERIF;
  const re = /(`[^`]+`|\*\*[^*]+\*\*)/g;
  let last = 0, m;
  const push = (t, extra) => { if (t) out.push(new TextRun(Object.assign({ text: t, font, size }, o.bold ? { bold: true } : {}, extra))); };
  while ((m = re.exec(text))) {
    push(text.slice(last, m.index));
    const tok = m[0];
    if (tok[0] === '`') push(tok.slice(1, -1), { font: MONO, size: size - 2 });
    else push(tok.slice(2, -2), { bold: true });
    last = m.index + tok.length;
  }
  push(text.slice(last));
  return out;
}
const para = (text, o = {}) => new Paragraph({
  spacing: { after: o.after === undefined ? 160 : o.after, before: o.before || 0 },
  keepNext: !!o.keepNext, keepLines: !!o.keepLines,
  alignment: o.align, indent: o.indent, children: runs(text, o)
});

// ------------------------------------------------------------ block builders
let figNo = 0, chapNo = 0;
const toc = [];                    // {lvl, text, id}
const body = [];

function chapter(title, opts = {}) {
  const label = opts.plain ? title : `Chapter ${++chapNo}. ${title}`;
  const display = opts.appendix ? title : label;
  toc.push({ lvl: 1, text: display });
  body.push(new Paragraph({
    pageBreakBefore: !opts.first, keepNext: true,
    heading: HeadingLevel.HEADING_1, outlineLevel: 0,
    spacing: { before: 0, after: 240 },
    border: { bottom: { style: BorderStyle.SINGLE, size: 12, space: 4, color: '000000' } },
    children: [new TextRun({ text: display, font: SANS, bold: true, size: 32 })]
  }));
}
function h2(title) {
  toc.push({ lvl: 2, text: title });
  body.push(new Paragraph({
    heading: HeadingLevel.HEADING_2, outlineLevel: 1, keepNext: true,
    spacing: { before: 240, after: 120 },
    children: [new TextRun({ text: title, font: SANS, bold: true, size: 24 })]
  }));
}
function h3(title) {
  body.push(new Paragraph({
    keepNext: true, spacing: { before: 160, after: 80 },
    children: [new TextRun({ text: title, font: SANS, bold: true, size: 21 })]
  }));
}
const p = (t, o) => body.push(para(t, o));
function ul(items) {
  items.forEach((t, i) => body.push(new Paragraph({
    numbering: { reference: 'bul', level: 0 },
    spacing: { after: i === items.length - 1 ? 160 : 60 },
    children: runs(t)
  })));
}
function caption(text) {
  const n = ++figNo;
  body.push(new Paragraph({
    spacing: { before: 60, after: 200 },
    children: [new TextRun({ text: `Figure ${n}.`, font: SANS, bold: true, size: 17 }),
               new TextRun({ text: ' ' + text, font: SANS, size: 17 })]
  }));
  return n;
}
const thin = { style: BorderStyle.SINGLE, size: 4, color: '808080' };
const thinB = { top: thin, bottom: thin, left: thin, right: thin };

// figure with code (monospaced, grey frame)
function codeLines(text) {
  return text.replace(/\s+$/, '').split('\n').map(l => new Paragraph({
    spacing: { after: 0 },
    children: [new TextRun({ text: l === '' ? ' ' : l, font: MONO, size: 17 })]
  }));
}
function fig(cap, code) {
  body.push(new Table({
    width: { size: CONTENT_W, type: WidthType.DXA }, columnWidths: [CONTENT_W],
    rows: [new TableRow({ cantSplit: true, children: [new TableCell({
      width: { size: CONTENT_W, type: WidthType.DXA }, borders: thinB,
      shading: { fill: 'F2F2F2', type: ShadingType.CLEAR, color: 'auto' },
      margins: { top: 100, bottom: 100, left: 140, right: 140 },
      children: codeLines(code)
    })] })]
  }));
  caption(cap);
}

// strip the function-definition chatter ("     Z<-NAME X" / "     NAME defined.")
function cleanOutput(out) {
  const lines = out.replace(/\n$/, '').split('\n');
  const res = [];
  for (let i = 0; i < lines.length; i++) {
    if (/^     \S/.test(lines[i]) && i + 1 < lines.length && /^     \S+ defined\.$/.test(lines[i + 1])) { i++; continue; }
    res.push(lines[i]);
  }
  return res.join('\n');
}
function runApl(file, stdin) {
  const r = cp.spawnSync(APL, [file], { input: stdin || '', encoding: 'utf8', cwd: ROOT });
  return (r.stdout || '') + (r.stderr || '');
}
// a program followed by what it prints
function run(cap, file, stdin, o = {}) {
  const src = fs.readFileSync(path.join(ROOT, file), 'utf8').replace(/\s+$/, '');
  const out = cleanOutput(runApl(file, stdin));
  let text = src;
  if (o.cmd) text = '$ ' + o.cmd + '\n' + out;
  else text = src + '\n\n# prints\n' + out;
  fig(cap, text);
}
// a short snippet executed on the fly (the script is written to a temp file)
function snip(cap, code, o = {}) {
  const tmp = path.join(ROOT, 'docs', '_snip.apl');
  fs.writeFileSync(tmp, code.replace(/\s+$/, '') + '\n');
  const out = cleanOutput(runApl(tmp, o.stdin));
  fs.unlinkSync(tmp);
  fig(cap, code.replace(/\s+$/, '') + '\n\n# prints\n' + out);
}
const show = (cap, text) => fig(cap, text);

// table: head=[..], rows=[[..]], widths sum to CONTENT_W; mono = column indexes in code font;
// sym = column indexes holding APL glyphs
function table(cap, head, rows, widths, o = {}) {
  const mono = o.mono || [], sym = o.sym || [];
  const tot = widths.reduce((a, b) => a + b, 0);
  if (tot !== CONTENT_W) throw new Error('table widths ' + tot + ' for ' + cap);
  const keep = rows.length <= 14;
  const cell = (t, i, isHead) => new TableCell({
    width: { size: widths[i], type: WidthType.DXA }, borders: thinB,
    shading: isHead ? { fill: 'D9D9D9', type: ShadingType.CLEAR, color: 'auto' } : undefined,
    margins: { top: 50, bottom: 50, left: 100, right: 100 },
    children: [new Paragraph({
      spacing: { after: 0 }, keepNext: keep,
      children: isHead ? [new TextRun({ text: t, font: SANS, bold: true, size: 17 })]
        : sym.includes(i) ? [new TextRun({ text: t, font: SYM, size: 19 })]
        : mono.includes(i) ? [new TextRun({ text: String(t), font: MONO, size: 17 })]
        : runs(String(t), { size: 19 })
    })]
  });
  body.push(new Table({
    width: { size: CONTENT_W, type: WidthType.DXA }, columnWidths: widths,
    rows: [new TableRow({ tableHeader: true, cantSplit: true, children: head.map((h, i) => cell(h, i, true)) })]
      .concat(rows.map(r => new TableRow({ cantSplit: true, children: r.map((t, i) => cell(t, i, false)) })))
  }));
  caption(cap);
}

// ------------------------------------------------------------ the text
require('./manual_content.js')({ chapter, h2, h3, p, ul, fig, run, snip, show, table, fs, path, ROOT });

// ------------------------------------------------------------ front matter
const C = (text, o) => new Paragraph(Object.assign({ children: [new TextRun(Object.assign({ text, font: SANS }, o.run))] }, o.para || {}));
const cover = [
  new Paragraph({ spacing: { before: 2400, after: 0 }, children: [new TextRun({ text: 'APL', font: SANS, bold: true, size: 72 })] }),
  new Paragraph({ spacing: { after: 360 }, children: [new TextRun({ text: 'A Word-Glyph APL Interpreter in C', font: SANS, size: 30 })] }),
  new Paragraph({ spacing: { after: 0 }, children: [new TextRun({ text: 'Introduction', font: SANS, bold: true, size: 34 })] }),
  new Paragraph({ spacing: { after: 160 }, border: { bottom: { style: BorderStyle.SINGLE, size: 24, space: 6, color: '000000' } },
    children: [new TextRun({ text: 'and User\u2019s Guide', font: SANS, bold: true, size: 34 })] }),
  new Paragraph({ spacing: { before: 200, after: 40 }, children: [new TextRun({ text: 'Release 0.4', font: SANS, size: 20 })] }),
  new Paragraph({ spacing: { after: 1800 }, children: [new TextRun({ text: 'for ANSI C (C89); VM/370 CMS planned', font: SANS, size: 17 })] }),
  new Paragraph({ children: [new TextRun({ text: 'Publication ' + PUB, font: SANS, size: 17 })] })
];
const edition = [
  new Paragraph({ spacing: { before: 400, after: 200 }, children: [new TextRun({ text: 'First Edition (October 2026)', font: SANS, bold: true, size: 21 })] }),
  para('This edition applies to APL Release 0.4 (October 2026), an interpreter for the APL language written in portable ANSI C, and to all subsequent versions until otherwise indicated in new editions.'),
  para('APL is a hobbyist project. It follows APL2 and borrows selected extensions from Dyalog APL as closely as a small interpreter can. It is not an implementation of any particular commercial product, and it spells the APL glyphs as English words.'),
  para('APL2 and VM/370 are names of products of their respective owners; Dyalog is a trademark of Dyalog Ltd. Other names are trademarks of their respective owners.'),
  para('The project repository holds the interpreter source, the test programs, the examples and this manual, and every example in the manual can be run from it. Comments on this publication may be addressed to the project.')
];

function tocEntries() {
  const out = [new Paragraph({ spacing: { after: 240 }, children: [new TextRun({ text: 'Contents', font: SANS, bold: true, size: 32 })] })];
  toc.forEach((e, ti) => {
    const pg = PAGES[ti] !== undefined ? PAGES[ti] : '0';
    const ch = e.lvl === 1;
    out.push(new Paragraph({
      spacing: { before: ch ? 100 : 0, after: ch ? 20 : 0 },
      indent: ch ? undefined : { left: 400 },
      tabStops: [{ type: TabStopType.RIGHT, position: CONTENT_W, leader: 'dot' }],
      children: [new TextRun({ text: e.text + '\t' + pg, font: ch ? SANS : SERIF, bold: ch, size: ch ? 21 : 20 })]
    }));
  });
  return out;
}

const pageProps = (top) => ({ page: { size: { width: 11906, height: 16838 }, margin: { top: top || 1134, right: 1134, bottom: 1134, left: 1134, header: 567, footer: 567 } } });
const footer = new Footer({ children: [new Paragraph({ children: [
  new TextRun({ text: PUB + ' ', font: SANS, size: 16 }), new TextRun({ children: [PageNumber.CURRENT], font: SANS, size: 18 })] })] });
const header = new Header({ children: [new Paragraph({
  border: { bottom: { style: BorderStyle.SINGLE, size: 4, space: 4, color: '000000' } },
  children: [new TextRun({ text: HDR, font: SANS, size: 16 })] })] });

const doc = new Document({
  creator: 'APL project', title: 'APL Introduction and User\u2019s Guide',
  styles: {
    default: { document: { run: { font: SERIF, size: 21 } } },
    paragraphStyles: [
      { id: 'Heading1', name: 'Heading 1', basedOn: 'Normal', next: 'Normal', quickFormat: true, run: { font: SANS, size: 32, bold: true }, paragraph: { outlineLevel: 0 } },
      { id: 'Heading2', name: 'Heading 2', basedOn: 'Normal', next: 'Normal', quickFormat: true, run: { font: SANS, size: 24, bold: true }, paragraph: { outlineLevel: 1 } }
    ]
  },
  numbering: { config: [{ reference: 'bul', levels: [{ level: 0, format: LevelFormat.BULLET, text: '\u25CF', alignment: AlignmentType.LEFT,
    style: { paragraph: { indent: { left: 720, hanging: 360 } }, run: { font: SANS, size: 14 } } }] }] },
  sections: [
    { properties: pageProps(), children: cover },
    { properties: Object.assign({ type: SectionType.NEXT_PAGE }, pageProps()), children: edition },
    { properties: Object.assign({ type: SectionType.NEXT_PAGE }, pageProps()), children: tocEntries() },
    { properties: Object.assign({ type: SectionType.NEXT_PAGE }, pageProps(1300), { page: Object.assign({}, pageProps(1300).page, { pageNumbers: { start: 1 } }) }),
      headers: { default: header }, footers: { default: footer }, children: body }
  ]
});
Packer.toBuffer(doc).then(buf => { fs.writeFileSync(OUT, buf); fs.writeFileSync(path.join(ROOT, 'docs', '_toc.json'), JSON.stringify(toc)); console.log('wrote', OUT, 'figures:', figNo); });
