module.exports = ({ chapter, h2, h3, p, ul, fig, run, snip, show, table, fs, path, ROOT }) => {
  // ======================================================== 7
  chapter('Examples');
  p('The programs in this chapter are in the directory `examples` of the repository, and each figure shows what the interpreter printed for it when this book was built. `make test` runs the ones that need no input and compares the output with the stored copy.');
  h2('Prime numbers');
  p('A prime has no divisors but itself and 1. The program makes a table of the remainders of every number from 2 to 30 divided by every candidate divisor, counts the zeros in each column, and keeps the numbers that have exactly one divisor, which is the number itself. The table is built by the `outer` operator with `mod`, and the counting by `reduce plus[1]`, which adds down the columns. There is no loop.');
  run('examples/primes.apl', 'examples/primes.apl');
  h2('Statistics');
  p('Three small functions that call each other: `STDDEV` uses `MEAN`. `MEDIAN` sorts with `gradeup` and takes the middle one or two items. The functions are defined with `nabla` and used like the primitives.');
  run('examples/stats.apl', 'examples/stats.apl');
  h2('The game of life');
  p('Conway’s Game of Life on a six by six grid whose edges wrap around. `NEIGHBOURS` counts the live neighbours of every cell by adding the grid to copies of itself rotated by one place in each direction, with `rotate` along the first axis (`rotate[1]`) and the last. `STEP` applies the rules. `SHOW` prints a grid by using the 0s and 1s as an index into the two characters blank and `#`. The picture is a glider; it moves one cell diagonally in four steps.');
  run('examples/life.apl', 'examples/life.apl');
  h2('A guessing game');
  p('The program picks a number with `roll` after setting `[]RL`, so that every run picks the same number. It then reads guesses with `[]` and answers `lower` or `higher` until the guess is right. The function uses labels and conditional branches; it could be written with `:While` as well. The figure shows a run where the guesses came from a pipe.');
  run('A run of examples/guess.apl', 'examples/guess.apl', '50\n25\n10\n5\n3\n');
  h2('The example files');
  table('The example programs',
    ['File', 'What it shows'],
    [['hello.apl', 'The smallest program'],
     ['ask.apl', 'Reading from the keyboard'],
     ['arrays.apl', 'Shape, indexing, axes'],
     ['nested.apl', 'Nested arrays'],
     ['scalar.apl', 'Scalar functions'],
     ['structure.apl', 'Sorting, searching, sets, partitions'],
     ['strings.apl', 'Text'],
     ['operators.apl', 'The operators'],
     ['lambda.apl', 'Lambdas, recursion and guards'],
     ['control.apl', 'Control words'],
     ['userop.apl', 'Operators defined with dop'],
     ['procs.apl', 'Shared variables and processors'],
     ['bank.apl', 'Functions, locals, branches, error trapping'],
     ['stats.apl', 'Statistics'],
     ['life.apl', 'The game of life'],
     ['primes.apl', 'The prime sieve'],
     ['guess.apl', 'A guessing game'],
     ['basic_smoke_test.apl, edge_cases_test.apl, functions_demo.apl, matrix_demo.apl, text_demo.apl', 'Test programs inherited from the REXX prototype']],
    [4200, 5438], { mono: [0] });

  // ======================================================== 8
  chapter('How the Interpreter Works');
  h2('The parts');
  p('The interpreter is written in C89 as one translation unit. `src/apl.c` includes the parts in the order of the table below; `tools/amalg.sh` expands the includes into the single file `apl_all.c`, which `make` compiles. Every function and variable is `static`; only `main` is visible to the linker.');
  table('The source files',
    ['File', 'Contents'],
    [['p0_base.c', 'Headers, limits, character classes, memory allocation, the error mechanism'],
     ['p1_val.c', 'Arrays: types, reference counts, constructors, simplification, display'],
     ['p2_lex.c', 'The tokenizer, and the registry that frees temporaries after an error'],
     ['p3_prim.c', 'The scalar and structural primitives, and the table of words'],
     ['p3b_ap.c', 'Auxiliary processors and shared variables: the hooks and the demonstration processors'],
     ['p4_eval.c', 'Variables, user functions, control words, the parser and evaluator, the operators'],
     ['p5_sys.c', 'The function editor, system functions and commands, workspaces, the session loop and `main`']],
    [2000, 7638], { mono: [0] });
  h2('Values');
  p('A value is a reference-counted array with a type, a rank, a shape and its items. The type is one of three: `T_NUM` holds doubles, `T_CHR` holds characters, and `T_NEST` holds pointers to other values. After a function builds a nested array, `a_simplify` turns it into a numeric or character array if every item is a simple scalar, so there is one representation for each value.');
  h2('Evaluation');
  p('The evaluator is a recursive-descent parser that evaluates as it parses. `parse_expr` reads one operand (a strand of items), looks at the token after it, and decides whether it is a dyadic function, an operator or the end; if it is a function it evaluates everything to the right first, which is what gives right-to-left evaluation. Operators are looked up the same way, before their operands. A function written in braces is turned into a statement list once and cached by its text. Control words are recognized in a user function when it is first called: the matching `:End` of each is found by a scan and the jumps are stored with the function, so a loop costs no more than a branch.');
  h2('Errors');
  p('An error is raised with `rt_error` or `syn_error`, which fill in the message and call `longjmp` to the nearest handler: the session loop, or the `[]EA` that is running. Temporary buffers and token arrays are entered in a registry, and the handler releases whatever the failed statement still held. Local variables of the functions that were interrupted are restored by unwinding the call frames.');
  h2('Limits and portability');
  p('The program is written so that it can be compiled for VM/370, where the available C compiler is GCC 3.8 (GCCLIB). That decides several choices: there are no 64-bit integers, so array sizes and indexes are 32-bit `int` and numbers are `double`; floating point on that machine is hexadecimal, so the largest number is about 7.2e75 and the interpreter reports a DOMAIN ERROR beyond 7.0e75 on every machine, so that results agree; the character set is EBCDIC, so no code compares character values with ranges; external names are limited to eight characters, which is why nothing but `main` is external; and the source must fit in 80-column records. Chapter 10 lists what is still to be done for that port.');
  h2('Adding a primitive');
  ul([
    'Add an identifier to the enumeration at the top of `p3_prim.c` and a line to the table `PRIMS`: the word, the identifier, and whether it has a monadic and a dyadic form.',
    'Write the function, taking and returning `Arr *`. Arguments belong to the caller; the result is a new reference. Raise errors with `rt_error`.',
    'Add a `case` to `prim_mon0` or `prim_dya0`. A scalar function is added to `sdya0` or `smon0` instead and gets pervasion for free.',
    'Add a test program under `tests/scen`, run it, check the output by eye, and keep it as the expected output.'
  ]);
  h2('Writing an auxiliary processor');
  p('A processor is a set of up to four C functions, called hooks, that are registered in `ap_init` in `p3b_ap.c`. The variable layer calls them: `offer` when `[]SVO` offers a variable, `get` when the variable is referenced, `set` after it is assigned, and `retract` when the offer ends. A hook that is not needed is a null pointer, and then the variable behaves like an ordinary one. `get` returns a new value for the reference, or null to supply the value that was last stored; `set` returns a new value to store, or null to keep the assigned value. Hooks report errors with `rt_error`. The figure below is the whole of the UPPER processor.');
  const src = fs.readFileSync(path.join(ROOT, 'src/p3b_ap.c'), 'utf8');
  const m1 = src.match(/\/\* AP 2: UPPER[\s\S]*?\n}\n/);
  const m2 = src.match(/static void ap_init\(void\)[\s\S]*?\n}\n/);
  const reg = m2[0].split('\n').filter(l => /UPPER|ap_init|^{|^}|ap_n = 0/.test(l)).join('\n');
  show('A processor: AP 2 (UPPER)', m1[0].replace(/\n$/, '') + '\n\n' + reg.replace(/\n$/, ''));
  p('The numbers 124, 126 and 210 are registered with `installed` set to 0, which makes `[]SVO` refuse them. To install one, write its hooks, change the registration to `installed = 1` and rebuild. A processor in C can hold any state it needs; it can keep a file open, for instance, and make each assignment to its variable a write and each reference a read. The hooks run in the middle of the evaluation of a statement, so they should be quick and they must leave the arrays they were given alone.');

  // ======================================================== 9
  chapter('Messages');
  p('Every message is printed on one line, preceded by `apl: runtime error --` or `apl: syntax error --`. The text after the dashes is the message. The families below are shared with APL2: those that start with a name in capitals also set `[]EN`.');
  table('Run time messages',
    ['Message', 'Meaning'],
    [['DOMAIN ERROR: text', 'The argument has the wrong kind of value or an impossible value: text for a number, division by zero, a number beyond 7.0e75, a count that is not a whole number, a singular matrix.'],
     ['RANK ERROR: text', 'The argument has the wrong number of axes, for instance a matrix where a vector is needed.'],
     ['LENGTH ERROR: text', 'The shapes of the arguments do not agree.'],
     ['AXIS ERROR: text', 'The axis in brackets is not a number or does not exist.'],
     ['INDEX ERROR: subscript out of range', 'An index is below the origin or above the length.'],
     ['DEPTH ERROR: text', 'Functions or loops are nested more deeply than the interpreter allows.'],
     ['WS FULL', 'The array would have more items than `[]WA`.'],
     ['[]: end of input', 'A bare `[]` read and there was no more input.'],
     ['[]IO must be 0 or 1  (and similar)', 'A system variable was assigned an impossible value.'],
     ['until: no convergence after 1000000 steps', 'The test of `until` never became true.'],
     ['auxiliary processor n (name) is reserved but not installed', 'A variable was offered to a number that has no code behind it.'],
     ['no such auxiliary processor: n', 'A variable was offered to a number that is not registered.']],
    [3800, 5838]);
  table('Syntax messages',
    ['Message', 'Meaning'],
    [['unexpected end of statement', 'A function or an operand is missing at the end.'],
     ['unexpected token near x', 'A word or symbol that does not fit at that place.'],
     ['undefined variable: x', 'x has no value.'],
     ['missing left argument for f', 'A dyadic-only function was used with one argument.'],
     ['missing function after op', 'An operator is not followed by a function.'],
     ['missing closing parenthesis, missing ] in index, missing } in lambda', 'Unbalanced brackets.'],
     ['unterminated string', 'A quote was opened and not closed.'],
     ['cannot assign to reserved word: x', 'x is the name of a primitive, an operator or a control word.'],
     ['name is already a function: x', 'x names a function, so it cannot be a variable.'],
     ['function returned no value', 'A function with no result was used where a value is needed.'],
     ['function is not monadic / not dyadic', 'A function was called with the wrong number of arguments.'],
     ['missing :End word in function  /  unbalanced :End word', 'The control words of a function do not pair up.'],
     ['a dop body must use alphaalpha', 'An operator definition does not use its operand.']],
    [3800, 5838]);

  // ======================================================== 10
  chapter('Restrictions and Known Problems');
  h2('Limits');
  ul([
    'Numbers are double-precision floating point. The largest magnitude is 7.0e75; anything beyond it is a DOMAIN ERROR. Whole numbers are exact up to 2^53, and integers used as indexes or counts must fit in 31 bits.',
    'An array has at most eight axes and at most 16,777,216 items. A line is at most 1,024 characters. Function calls may nest 200 deep, and `[]EA` 32 deep.',
    'Text is single-byte. Codes 0 to 255 can be made with `ucs`; what they print as depends on the terminal.'
  ]);
  h2('Language features that are not implemented');
  ul([
    '**Complex numbers.** A function that would give one, such as the square root of a negative number, is a DOMAIN ERROR.',
    '**Selective assignment** such as `(2 take v) <- ...`. Use indexed assignment or `amend`.',
    '**Operators written with a function header**, as in APL2. The `dop` form replaces them, and its operands must be functions.',
    '**The []FMT function**, which has a complicated format specification. `format` covers the common cases.',
    '**The control words** `:AndIf`, `:OrIf`, `:Trap`, `:With`, `:Section`, `:GoTo`, `:Access` and the rest of the Dyalog set.',
    '**Namespaces, classes, files and the []FIO and []NA interfaces.** The auxiliary processors are the way to reach the outside, and no file or screen processor exists yet.',
    '**Shared variables between programs.** The hooks connect a variable to a processor in the same process. `[]SVC`, `[]SVS`, `[]SVE`, `[]SVN` and the multi-user coupling states are not provided, and the value returned by `[]SVO` is always 1.',
    '**Dyalog primitives** for which no word exists: interval index (dyadic ⍸), stencil, variant, dyadic `⊂`, and left arguments to monadic operators other than those documented.',
    '**Other system names:** `[]LC`, `[]ML`, `[]FC`, `[]DR`, `[]CS`, `[]LX`, `[]ET`, `[]EM` and the rest not listed in Chapter 5.'
  ]);
  h2('Known problems');
  ul([
    'The words of the language are reserved. A program written before a word was added may have used it as a name: `first`, `left`, `right`, `log`, `rank`, `find`, `where`, `split`, `union`, `power`, `roll`, `deal`, `swap`, `repeat`, `until`, `format`, `execute`, `over`, `atop`, `amend`, `keyed`, `nest`, `ucs`, `zilch`, `dop` and `notmatch` are reserved.',
    '`[]DL` waits by spinning on the processor clock, which wastes the processor; it is meant for short pauses only.',
    '`)SAVE` does not record which variables are shared; after `)LOAD` they are ordinary variables with their last values.',
    'A function that was interrupted by an error inside a `:For` loop leaves the loop’s variable set in the workspace, and a few bytes of memory for the loop are not released until the interpreter ends.',
    'The random number generator is a 32-bit xorshift. It gives the same numbers on every platform for the same `[]RL` but is not the generator of any other APL.',
    '`[]PP` counts decimal places, not significant digits, so very small numbers print as 0 unless `[]PP` is raised. Large numbers print in exponent form.'
      ]);
  h2('Still to do for VM/370');
  p('The language is complete enough for the port to begin, and the port is the next step. It needs: a deck-build step that folds the roughly one hundred source lines longer than 71 columns; a declaration of `setjmp` for GCCLIB under `__CMS__`; a single TEXT file for the loader; and a test on a Hercules VM/370 system. The first auxiliary processor to write there will be AP210, the CMS file system.');

  // ======================================================== Appendices
  chapter('Appendix A. Commands', { plain: true, appendix: true });
  table('Commands',
    ['Command', 'Action'],
    [[')VARS', 'List the variables.'],
     [')FNS', 'List the functions, including named lambdas and operators.'],
     [')PRIMS', 'List the words, operators, system names and control words.'],
     [')PROCS', 'List the auxiliary processors and the variables shared with them.'],
     [')LIB', 'List the saved workspaces in the current directory.'],
     [')WSID [name]', 'Show or set the workspace name.'],
     [')CLEAR', 'Remove all variables and functions; retract shared variables; set the origin to 1 and the precision to 6.'],
     [')SAVE [name]', 'Save the workspace as the source file name.apl.'],
     [')LOAD name', 'Clear the workspace and load a saved one.'],
     [')COPY name [objects]', 'Bring everything, or the named objects, from a saved workspace.'],
     [')ERASE names', 'Erase variables or functions.'],
     [')HELP  or  )?', 'Print a summary.'],
     [')QUIT  )EXIT  )OFF', 'End the session.']],
    [3000, 6638], { mono: [0] });
  p('A command is not case sensitive. In a script, a command line is executed as it is read.');
  chapter('Appendix B. The Repository', { plain: true, appendix: true });
  table('The repository',
    ['Path', 'Contents'],
    [['src/', 'The six parts of the interpreter and `apl.c`'],
     ['apl_all.c', 'The single-file form, generated by `tools/amalg.sh`'],
     ['Makefile', 'Targets all, test and clean'],
     ['examples/', 'The example programs'],
     ['tests/', '`run.sh` and the scenario tests in `tests/scen`, with expected output'],
     ['tools/', '`amalg.sh`'],
     ['docs/', 'This manual, `APL-User-Guide.docx`, and the program that generates it'],
     ['ref/', 'The REXX prototype (`apl.rex`), its documentation and reference outputs']],
    [2400, 7238], { mono: [0] });
  p('To regenerate this book, build the interpreter and run `node docs/build_manual.js` from the top directory. The figures are produced by running the interpreter, so they cannot go out of date without the next build showing it.');
  chapter('Appendix C. Differences from APL2 and Dyalog APL', { plain: true, appendix: true });
  p('The first figure lists the differences in how a program is written. The second lists differences in behavior that a reader who knows APL might trip over.');
  table('Differences in notation',
    ['APL2 and Dyalog', 'This interpreter'],
    [['Glyphs, such as ⍴ ⍳ ⌽', 'Words: `rho`, `iota`, `reverse`; see Chapter 6'],
     ['`←`  assignment, `→`  branch', '`<-` and `->`'],
     ['`∇` to define a function', '`nabla`, or `edit`'],
     ['`⎕` before a system name', '`[]` before it: `[]IO`, `[]EA`'],
     ['`⍝` comment', '`#` or `lamp`'],
     ['`⍬` zilch', '`zilch`'],
     ['Operators between their operands: `+/v`, `f¨v`', 'Operators before them: `reduce plus v`, `each f v`'],
     ['`*` is power, `/` is reduce or compress, `|` is residue', '`*` is times, `/` is divide, `mod` is residue; `power`, `reduce`, `compress`'],
     ['`⍺`, `⍵`, `∇` in a dfn; statements separated by `⋄`', '`alpha`, `omega`, `self`; statements separated by `;`; guards as `cond : value`'],
     ['Dop operands `⍺⍺` `⍵⍵`', '`alphaalpha`, `omegaomega`'],
     ['`:If` ... and `:For` are Dyalog only', 'The same words, any case, one per line']],
    [4800, 4838]);
  table('Differences in behavior',
    ['Topic', 'Difference'],
    [['Complex numbers', 'Not supported.'],
     ['`[]PP`', 'Counts decimal places, not significant digits.'],
     ['Nested display', 'No boxes or indentation: an array item has a blank before it.'],
     ['`keyed`, `amend`', 'Keyed takes a vector of keys. Amend works on vectors, with an index list only.'],
     ['`reduce` and `scan`', 'Both work along the last axis unless `[k]` is given; `n reduce` is the n-wise reduction.'],
     ['`rank`', 'Takes one to three numbers, as in Dyalog.'],
     ['Random numbers', '`roll` and `deal` use a 32-bit xorshift generator.'],
     ['`[]SVO`', 'Always returns 1 for an accepted offer; there are no coupling degrees.'],
     ['Names', 'Case sensitive. Reserved words cannot be used as names.'],
     ['Output of numbers', 'A negative number is printed with a minus sign; very large numbers are printed with an exponent.'],
     ['Numbers beyond 7.0e75', 'DOMAIN ERROR on every machine, to match the VM/370 range.']],
    [2400, 7238]);
  h2('Changes from the REXX prototype');
  p('The interpreter began as a prototype written in Regina REXX. The C version keeps its commands, its editor and its programs, and differs from it in the following ways, which show in the examples: `mod` follows APL, so that `a mod b` is the remainder of b divided by a; the reduction of an empty array gives the identity element instead of an error; `0 divide 0` is 1; arrays may be nested and have up to eight axes; and the numbers have a limited range.');
};
