module.exports = ({ chapter, h2, h3, p, ul, fig, run, snip, show, table, fs, path, ROOT }) => {
  // ======================================================== 4
  chapter('Arrays and Nested Arrays');
  h2('Shape and rank');
  p('Every value is an array. Its **shape** is the list of its lengths along each axis; its **rank** is the number of axes. A single number or character is a **scalar** (rank 0, empty shape), a list is a **vector** (rank 1), a table is a **matrix** (rank 2). The interpreter supports ranks up to 8. `rho x` returns the shape and `rank` of the shape is its length. `s rho x` reshapes: it builds an array of shape s from the items of x, repeating them if there are too few.');
  p('Items are stored in row-major order: the last axis changes fastest. An array may be empty, with a zero in its shape; `iota 0` is the empty numeric vector, and the word `zilch` stands for it directly.');
  h2('Writing arrays');
  p('Numbers or texts written side by side form a vector: `10 20 30`. Several items in a row form a vector of those items; if the items are not all single numbers or all single characters, the result is a **nested vector**: `(1 2) (3 4 5) \'abc\'` has three items, each an array. Parentheses group; a single item in parentheses is that item.');
  h2('Indexing');
  p('A name followed by brackets selects items: `v[2 4]`, and for a matrix `m[2;3]`, `m[2;]` (row 2), `m[;1]` (column 1), `m[1 3;2 3]` (a submatrix). The positions count from `[]IO`, which is 1 unless you set it to 0. An index may itself be an array and the result takes its shape. An out-of-range index is an `INDEX ERROR`.');
  p('The same bracket form on the left of `<-` assigns: `m[2;3] <- 0`. The function `index` does the same selection as a function: `v index 2 3`.');
  h2('Axes');
  p('Some functions work along one axis. By default it is the last axis. A bracketed number after the word chooses another: `reverse[1] m` reverses the rows, `reduce plus[1] m` adds down the columns, `1 rotate[1] m`, `compress[1]`, `expand[1]`, `cat[1]`. The axis number counts from `[]IO`.');
  run('Shape, indexing and axes (examples/arrays.apl)', 'examples/arrays.apl');
  h2('Nested arrays');
  p('`enclose x` makes an array into a single item (a scalar that contains it); `disclose` (or `mix`) does the reverse, padding short items with zeros or blanks so that the items fit one array. `first` returns the first item. `depth` is 0 for a simple scalar, 1 for a simple array, 2 for an array of arrays, and so on. `tally` counts the items along the first axis. `n pick x` selects the item at the position n; a list of positions picks through the levels. `enlist` returns all the simple items as one vector.');
  p('A nested array whose items are all simple scalars is automatically replaced by the equivalent simple array; there is no way to tell them apart afterwards. A reduction of a nested vector gives a scalar that contains the result, which is displayed with a leading blank.');
  p('When a nested array is displayed each item is shown in turn. An item that is itself an array is shown with a blank before it and the items are separated by blanks. A one-item nested array therefore starts with a blank, which is how you tell an enclosed value from a plain one.');
  run('Nested arrays (examples/nested.apl)', 'examples/nested.apl');
  p('Most functions work on nested arrays item by item. `each f x` applies a function to each item of an array, whatever its structure; for a scalar function such as `plus`, the function already goes down through the levels, so `each` is needed only for functions that treat their argument as a whole.');
  h2('Text');
  p('Text is an array of characters, so the structural functions apply to it: `reverse`, `take`, `drop`, `cat`, `compress`, `gradeup` and so on. A comparison works on characters and gives 0 or 1 for each. `member` tests membership. `format` converts numbers to text and `execute` evaluates text as an expression. A character matrix is made with `rho`.');
  run('Working with text (examples/strings.apl)', 'examples/strings.apl');

  // ======================================================== 5
  chapter('Console Input and Output, and the System');
  h2('Input and output');
  p('A bare `[]` (or the word `quad`) in an expression reads a line from standard input, evaluates it and returns its value. `[]<-x` (or `quad <- x`) prints x; it is an alternative to writing the expression on a line of its own. An expression statement prints its value unless the value is the result of an assignment. A function with no result prints nothing.');
  p('A value is printed in a form that can be read back: numbers separated by blanks, a character vector as its characters, a matrix with one row per line and the columns aligned. An empty array prints as an empty line.');
  h2('System variables');
  p('Names that start with `[]` and are in capitals belong to the system. The ones that can be assigned are checked when you assign them; assigning a read-only one is an error.');
  table('System variables',
    ['Name', 'Meaning', 'Default'],
    [['[]IO', 'Index origin, 0 or 1. Affects `iota`, indexing, `roll`, `deal`, `gradeup`, `where`.', '1'],
     ['[]PP', 'Printing precision: places after the decimal point, 0 to 17.', '6'],
     ['[]CT', 'Comparison tolerance, from 0 to 1. Two numbers are equal if they differ by no more than this fraction of the larger.', '1e-13'],
     ['[]RL', 'Random seed, an integer from 1 to 4294967295. Assigning it restarts the generator, so a program can repeat a sequence.', '16807'],
     ['[]TS', 'The time stamp: year, month, day, hour, minute, second. Read-only.', ''],
     ['[]A, []D', 'The upper-case letters, and the digits. Read-only.', ''],
     ['[]AV, []TC', 'The 256 characters; the backspace, return and line feed characters. Read-only.', ''],
     ['[]WA', 'The largest number of items an array may have. Read-only.', '16777216'],
     ['[]AI', 'Account information: user, compute time and connect time in milliseconds, and keying time. Only the second and third are measured.', ''],
     ['[]DM, []EN', 'The message and the number of the last error. Read-only.', '']],
    [1500, 6838, 1300], { mono: [0] });
  h2('System functions');
  p('System functions have names in capitals after `[]`. The monadic ones take everything to their right.');
  table('System functions',
    ['Function', 'Result'],
    [['[]NC \'name\'', 'The class of a name: 0 unused, 2 variable, 3 function. One result per row of a text matrix.'],
     ['[]EX \'name\'', 'Erases the variable or function; returns 1 if it existed.'],
     ['[]CR \'name\'', 'The text of a function as a character matrix, header first; an empty vector if there is no such function.'],
     ['[]FX rows', 'Defines a function from a character matrix or a vector of texts and returns its name. The first row is the header.'],
     ['\'expr\' []EA \'alt\'', 'Evaluates expr; if it fails, evaluates alt.'],
     ['[]SIGNAL n  or  []SIGNAL \'text\'', 'Raises an error. The numbers are 1 (workspace full), 2 (syntax), 4 (rank), 5 (length), 6 (value), 7 (index), 10 (axis) and 11 (domain).'],
     ['[]DL secs', 'Waits that many seconds, up to 3600, by spinning on the clock; returns the time waited.'],
     ['[]VFI \'text\'', 'Validates and fixes input: returns a pair, a vector of 1s and 0s that says which blank-separated words are numbers, and the numbers (0 for the others).']],
    [2800, 6838], { mono: [0] });
  snip('Functions defined at run time', "rows <- 'Z<-DOUBLE N' 'Z<-N plus N'\n[]FX rows\nDOUBLE 21\n[]NC 'DOUBLE'\n[]CR 'DOUBLE'\n[]EX 'DOUBLE'\n[]NC 'DOUBLE'");
  h2('System commands');
  p('A line that starts with a right parenthesis is a system command and is not part of the language. Appendix A lists them all. The ones you will use most are `)VARS` and `)FNS` to list what is defined, `)CLEAR` to start again, `)SAVE`, `)LOAD` and `)COPY` for workspaces, and `)QUIT`.');
  h2('Workspaces');
  p('`)SAVE name` writes the variables and functions to the file `name.apl` in the current directory; the file is ordinary APL source, with one assignment per variable and the editor text of each function, so you can read it and edit it. `)LOAD name` clears the workspace and runs the file. `)COPY name` brings in everything from a saved workspace, or only the objects named after it. `)WSID name` sets the name that `)SAVE` uses by default. `)LIB` lists the saved workspaces in the directory.');
  p('Because a workspace is a source file, anything that can be said in a program can be saved and nothing else can: the open `:For` loops of a running function, for instance, are not kept.');
  h2('Shared variables and auxiliary processors');
  p('An **auxiliary processor** is a numbered module, outside the language, that owns some variables. A variable becomes shared with the processor when you offer it: `3 []SVO \'N\'` offers the variable N to processor 3. From then on, every use of N asks the processor for its value and every assignment to N tells the processor about the new value. `[]SVR \'N\'` retracts the offer. This is how APL2 reaches the outside world: files, the screen and other programs are processors, and a program reads and writes them by using variables.');
  table('Shared variable functions',
    ['Function', 'Result'],
    [['n []SVO \'names\'', 'Offers each name to processor n. Returns 1 for each offer that was accepted; an error if the processor rejects it or is not installed.'],
     ['[]SVO \'names\'', 'For each name, 1 if it is shared and 0 if it is not.'],
     ['[]SVR \'names\'', 'Retracts the offers. Returns 1 for each name that was shared.'],
     ['[]SVQ n', 'The names currently shared with processor n.'],
     [')PROCS', 'A system command: lists the processors, and what is shared with each.']],
    [2800, 6838], { mono: [0] });
  p('This release has the hooks and three demonstration processors, listed in the figure below. Numbers 124, 126 and 210 are reserved for processors that have not been written yet; offering a variable to one of them is an error. Chapter 8 explains how to add a processor of your own.');
  table('Processors in this release',
    ['No.', 'Name', 'Behavior'],
    [['1', 'LOOPBACK', 'Does nothing: a shared variable simply holds its value. Useful for testing the coupling.'],
     ['2', 'UPPER', 'Text assigned to a shared variable is changed to capitals; other values are rejected.'],
     ['3', 'COUNTER', 'Every reference to the variable yields the next whole number; assigning a number sets the counter.'],
     ['124', 'AP124', 'Reserved, not installed.'],
     ['126', 'AP126', 'Reserved, not installed.'],
     ['210', 'AP210', 'Reserved, not installed.']],
    [900, 1700, 7038], { mono: [0, 1] });
  run('Shared variables (examples/procs.apl)', 'examples/procs.apl');
  p('The offer is made for the variable, not for the value. Erasing a shared variable, or clearing the workspace, retracts the offer. A shared variable cannot be a function.');

  // ======================================================== 6
  chapter('Functions');
  p('This chapter lists every primitive. Each table gives the word, the APL glyph it stands for, and what the function does with one argument (monadic) and with two (dyadic). A dash means that the form does not exist. The words are those of the `)PRIMS` command.');
  h2('Scalar functions');
  p('A scalar function works on each item. If both arguments are arrays they must have the same shape; if one of them is a scalar, it is paired with every item of the other. The functions also work through a nested array, down to its simple items.');
  table('Arithmetic and logic',
    ['Word', 'Glyph', 'Monadic', 'Dyadic'],
    [['plus  +', '+', 'Same as the argument', 'Sum'],
     ['minus  -', '−', '—', 'Difference'],
     ['neg', '−', 'Negative', '—'],
     ['times  *', '×', 'Signum: 1, 0 or −1 (also `signum`)', 'Product'],
     ['divide  /', '÷', 'Reciprocal (also `recip`)', 'Quotient; `0 divide 0` is 1; any other division by zero is a DOMAIN ERROR'],
     ['power', '*', 'e to the power x (also `exp`)', 'x to the power y'],
     ['log', '⍟', 'Natural logarithm (also `ln`)', 'Logarithm of y to base x'],
     ['fact', '!', 'Factorial', 'Binomial: `a fact b` is the number of ways to choose a of b'],
     ['circle', '○', 'Pi times x', 'Trigonometric: `n circle x`; see the figure after next'],
     ['max', '⌈', '—', 'Larger of the two'],
     ['min', '⌊', '—', 'Smaller of the two'],
     ['floor', '⌊', 'Largest integer not above x, within []CT', '—'],
     ['ceil', '⌈', 'Smallest integer not below x', '—'],
     ['abs', '|', 'Absolute value', '—'],
     ['mod', '|', '—', '`a mod b`: the remainder of b divided by a, with the sign of a; `0 mod b` is b'],
     ['and', '∧', '—', 'Boolean and; for other numbers, the least common multiple'],
     ['or', '∨', '—', 'Boolean or; for other numbers, the greatest common divisor'],
     ['nand', '⍲', '—', 'Not and'],
     ['nor', '⍱', '—', 'Not or'],
     ['not', '~', 'Boolean not', '—'],
     ['roll', '?', 'Random whole number from the origin to x; `roll 0` is a random fraction between 0 and 1', '—'],
     ['= < > <= >= <>', '= < > ≤ ≥ ≠', '—', 'Comparison, giving 1 or 0, with tolerance []CT']],
    [1900, 900, 3300, 3538], { mono: [0], sym: [1] });
  table('Circle functions: n circle x',
    ['n', 'Result', 'n', 'Result'],
    [['0', 'sqrt(1 − x²)', '−0', 'same as 0'],
     ['1', 'sin x', '−1', 'arcsin x'],
     ['2', 'cos x', '−2', 'arccos x'],
     ['3', 'tan x', '−3', 'arctan x'],
     ['4', 'sqrt(x² − 1)', '−4', 'same as 4'],
     ['5', 'sinh x', '−5', 'arcsinh x'],
     ['6', 'cosh x', '−6', 'arccosh x'],
     ['7', 'tanh x', '−7', 'arctanh x'],
     ['9', 'x', '10', 'abs x']],
    [900, 3919, 900, 3919]);
  snip('Scalar functions (examples/scalar.apl)', fs.readFileSync(path.join(ROOT, 'examples/scalar.apl'), 'utf8').replace(/^#.*\n/, ''));
  h2('Structure functions');
  table('Shape and structure',
    ['Word', 'Glyph', 'Monadic', 'Dyadic'],
    [['rho', '⍴', 'The shape of x', '`s rho x`: reshape x to shape s'],
     ['iota', '⍳', '1 to n (or 0 to n−1 when []IO is 0); for a vector, all the index vectors of that shape', '— (use `indexof`)'],
     ['ravel', ',', 'All items as a vector', '—'],
     ['cat', ',', '—', 'Join along the last axis (`cat[1]` the first)'],
     ['reverse', '⌽', 'Reverse along the last axis (`reverse[1]` first)', '—'],
     ['rotate', '⌽', '—', '`n rotate x`: rotate left by n along the last axis; negative rotates right'],
     ['transpose', '⍉', 'Reverse the order of the axes; for a matrix, swap rows and columns', '`p transpose x`: axis i of x becomes axis p[i]'],
     ['take', '↑', '—', '`n take x`: the first n items, or the last for negative n; padded with zeros or blanks'],
     ['drop', '↓', '—', '`n drop x`: remove the first n items (the last, if negative)'],
     ['compress', '/', '—', '`b compress x`: keep the items where b is 1; a number greater than 1 repeats'],
     ['expand', '\\', '—', '`b expand x`: put the items of x where b is 1, zeros or blanks elsewhere'],
     ['index', '⌷', '—', '`x index i`: select by index'],
     ['matmul', '+.×', '—', 'Matrix product; same as `x inner plus times y`'],
     ['domino', '⌹', 'Matrix inverse', '`b domino a`: solve the linear system a x = b by least squares'],
     ['tally', '≢', 'The number of items along the first axis', '—'],
     ['depth', '≡', 'The nesting depth', '—'],
     ['partition', '⊆', '—', '`p partition x`: a new group starts wherever p increases; items where p is 0 are dropped'],
     ['split', '↓', 'Cut a matrix into a vector of its rows', '—'],
     ['right  left', '⊢ ⊣', 'The argument itself', '`right` returns y and `left` returns x'],
     ['zilch', '⍬', 'The empty numeric vector (a value, not a function)', '']],
    [1500, 900, 3300, 3938], { mono: [0], sym: [1] });
  table('Nested arrays',
    ['Word', 'Glyph', 'Monadic', 'Dyadic'],
    [['enclose', '⊂', 'Make an array into one item', '—'],
     ['disclose  mix', '⊃', 'Make the items of an array into one array, padding', '—'],
     ['first', '↑', 'The first item', '—'],
     ['pick', '⊃', '—', '`n pick x`: select one item, through several levels if n is a list'],
     ['nest', '⊆', 'Enclose unless the argument is already a scalar or nested', '—'],
     ['enlist', '∊', 'All the simple items as a vector', '—'],
     ['match', '≡', '—', 'Same shape and the same items, at every level'],
     ['notmatch', '≢', '—', 'Not match']],
    [1900, 900, 3300, 3538], { mono: [0], sym: [1] });
  h2('Searching, sorting and sets');
  table('Search, sort and set functions',
    ['Word', 'Glyph', 'Monadic', 'Dyadic'],
    [['indexof', '⍳', '—', '`x indexof y`: where each item of y is found in x; one more than the length if absent'],
     ['member', '∊', '—', '`x member y`: 1 for each item of x found in y'],
     ['find', '⍷', '—', '`x find y`: 1 where a copy of x starts in y'],
     ['where', '⍸', 'The positions of the 1s (a number n repeats the position n times)', '—'],
     ['gradeup', '⍋', 'The indexes that put x in ascending order', '`c gradeup x`: sorted by the collating sequence c, which is a text'],
     ['gradedown', '⍒', 'Indexes for descending order', 'With a collating sequence'],
     ['unique', '∪', 'The distinct items, in order of first appearance', '—'],
     ['nubsieve', '≠', '1 for the first appearance of each item', '—'],
     ['union', '∪', '—', 'x followed by the items of y that are not in x'],
     ['intersect', '∩', '—', 'The items of x that are also in y'],
     ['without', '~', '—', 'The items of x that are not in y']],
    [1500, 900, 3300, 3938], { mono: [0], sym: [1] });
  run('Sorting, searching and sets (examples/structure.apl)', 'examples/structure.apl');
  h2('Conversion');
  table('Conversion and random numbers',
    ['Word', 'Glyph', 'Monadic', 'Dyadic'],
    [['format', '⍕', 'A number array as text', '`p format x`: with p places after the decimal point; `w p format x`: in a field of width w'],
     ['execute', '⍎', 'Evaluate text as an expression', '—'],
     ['encode', '⊤', '—', '`r encode n`: the digits of n in the mixed radix r'],
     ['decode', '⊥', '—', '`r decode d`: the value of the digits d in the radix r'],
     ['ucs', '⎕UCS', 'Characters to character codes, or codes to characters (0 to 255)', '—'],
     ['deal', '?', '—', '`m deal n`: m different random numbers from the first n']],
    [1500, 900, 3300, 3938], { mono: [0], sym: [1] });
  h2('Operators');
  p('The operators were introduced in Chapter 3. In every operator the functions are the operands and they are written, as here, before the array. Scalar functions are used directly as the operand of `reduce`, `scan` and `outer`; a function that you defined may be used as well and must then return a single number when its arguments are single numbers.');
  p('A reduction of an empty array gives the identity element of the function when there is one: 0 for `plus`, `minus`, `or`, `<>`, `<` and `>`; 1 for `times`, `divide`, `and`, `=`, `<=`, `>=` and `power`; the smallest number for `max` and the largest for `min`. For any other function it is a DOMAIN ERROR.');
  table('Operators in this release',
    ['Word', 'Glyph', 'Example', 'Result'],
    [['reduce', '/', 'reduce plus 1 2 3 4', '10'],
     ['scan', '\\', 'scan plus 1 2 3 4', '1 3 6 10'],
     ['each', '¨', 'each rho (1 2) (3 4 5)', 'the shapes of the two items'],
     ['outer', '∘.', '1 2 outer times 1 2 3', 'a 2 by 3 table'],
     ['inner', '.', '1 2 3 inner plus times 4 5 6', '32'],
     ['swap', '⍨', '2 swap minus 10', '8'],
     ['repeat', '⍣', 'repeat neg 3 5', '−5'],
     ['until', '⍣', 'until {omega times 2} {alpha > 100} 1', '128'],
     ['rank', '⍤', 'rank reverse 1 m', 'each row of m reversed'],
     ['over', '⍥', '3 over plus abs 4', '7'],
     ['atop', '∘', 'atop neg {reduce plus omega} 1 2 3', '−6'],
     ['amend', '@', '10 20 amend (2 4) iota 4', '1 10 3 20'],
     ['keyed', '⌸', 'x keyed f y', 'f applied to each distinct key']],
    [1300, 800, 4400, 3138], { mono: [0, 2], sym: [1] });
};
