/* ---------------------------------------------------------------------
   APL.REX  --  A small APL interpreter written in Regina REXX
   ---------------------------------------------------------------------
   Inspired by Rodnay Zacks' "A Microprogrammed APL Implementation"
   (the classic scan-driven, right-to-left, monadic/dyadic dispatch
   model).  Since APL's Greek-letter glyphs (rho, iota, etc.) are not
   part of the ASCII character set REXX works with, this dialect spells
   each primitive out as a lower-case word (or keeps the ordinary
   ASCII arithmetic symbols + - * / for the arithmetic primitives).

   Usage:
       rexx apl.rex                 -- start the interactive REPL
       rexx apl.rex program.apl     -- run a script file

   See regina_apl_doc.md for the full language reference.
   --------------------------------------------------------------------- */

NUMERIC DIGITS 20

call InitInterp

parse arg srcfile
if srcfile = '' then do
  call RunRepl
  exit 0
end

if \ FileExists(srcfile) then do
  say 'apl: cannot open source file:' srcfile
  exit 1
end

call RunFile srcfile
exit 0

/* ======================================================================
   InitInterp -- set up global tables (reserved words, variable store)
   ====================================================================== */
InitInterp: procedure expose FUNC. VARS. VARSET. VARLIST FUNCLIST OPLIST ALIASLIST,
                              USERFUNC. FNHDR. FNBODY. FNLINECOUNT. FNLIST
  drop FUNC. VARS. VARSET. USERFUNC. FNHDR. FNBODY. FNLINECOUNT.
  VARSET. = 0
  VARLIST = ''
  USERFUNC. = 0
  FNLIST = ''

  /* reserved primitive-function tokens (monadic and/or dyadic). Kept as
     one word list so both the parser's lookup table and the )PRIMS system
     command can be driven from a single source of truth. */
  FUNCLIST = 'rho iota take drop reverse rotate transpose ravel neg not',
             'abs cat compress index matmul floor ceil mod',
             '+ - * / = < > <= >= <> max min and or []EA'
  do i = 1 to words(FUNCLIST)
    call SetFunc word(FUNCLIST, i)
  end

  /* higher-order operators (prefix form), listed separately since
     they're parsed specially rather than looked up via FUNC. -- kept
     here only so )PRIMS has something to show for them. */
  OPLIST = 'reduce scan each outer'
  ALIASLIST = 'plus minus times divide'

  /* quad system variables / functions, spelled []NAME since the real
     glyph isn't ASCII.  []IO and []PP are settable; []TS is read-only. */
  call value 'APLIO', 1, 'ENVIRONMENT'    /* index origin, default 1 */
  call value 'APLPP', 6, 'ENVIRONMENT'    /* print precision (decimals) */
  call value 'APLCALLDEPTH', 0, 'ENVIRONMENT'   /* user-function call/recursion depth */
  call value 'APLBRANCH', '', 'ENVIRONMENT'     /* pending -> branch target, if any */
  call value 'APLSUPPRESSERR', '', 'ENVIRONMENT'  /* used by []EA to catch silently */
  call value 'APLWSID', 'CLEAR WS', 'ENVIRONMENT'   /* active workspace id, )WSID */
  return

SetFunc: procedure expose FUNC.
  parse arg nm
  FUNC.nm = 1
  return

FileExists: procedure
  parse arg fn
  rc = stream(fn, 'c', 'query exists')
  if rc = '' then return 0
  return 1

/* Workspace filenames are plain names without an extension (matching
   real APL's abstract workspace-name concept); on disk they're stored
   as NAME.apl. These two helpers convert between the two forms. */
WsFileName: procedure
  parse arg nm
  if translate(right(nm, 4)) = '.APL' then return nm
  return nm'.apl'

WsBaseName: procedure
  parse arg fn
  if translate(right(fn, 4)) = '.APL' then return left(fn, length(fn) - 4)
  return fn

/* WsResolveLoad: figures out which file a )LOAD name actually refers to.
   Tries, in order: name.apl first (the normal case, since that's what
   )SAVE produces), then the name exactly as given (so a workspace that
   happens to exist with no extension, or a different one, still loads).
   Returns the filename to open, or '' if neither exists. */
WsResolveLoad: procedure
  parse arg nm
  alt = WsFileName(nm)
  if FileExists(alt) then return alt
  if FileExists(nm) then return nm
  return ''

/* ======================================================================
   VALUE REPRESENTATION
   An APL value is a REXX string:   shape '|' ravel
     shape : space-separated list of dimension sizes ('' = scalar)
     ravel : space-separated list of numbers, row-major order
   Examples:
       scalar 5           ->   '|5'
       vector 1 2 3       ->   '3|1 2 3'
       matrix 2x3         ->   '2 3|1 2 3 4 5 6'
   ====================================================================== */

MkVal: procedure
  parse arg shp, rav
  return shp'|'rav

GetShape: procedure
  parse arg v
  parse var v shp '|' rav
  return shp

GetRavel: procedure
  parse arg v
  parse var v shp '|' rav
  return rav

Rank: procedure
  shp = GetShape(arg(1))
  if shp = '' then return 0
  return words(shp)

NumEl: procedure
  parse arg shp
  if shp = '' then return 1
  n = 1
  do i = 1 to words(shp)
    n = n * word(shp, i)
  end
  return n

IsScalar: procedure
  return Rank(arg(1)) = 0

/* TEXT VALUES -- a minimal, separate value kind for character strings
   ('like this'), used for messages and display rather than as a full
   character-array type. A text value is MkVal('TEXT', <the string>),
   which the ordinary numeric machinery never legitimately produces
   (numeric shapes only ever contain digits and spaces), so checking
   the shape field for the literal 'TEXT' is an unambiguous tag. */
MkText: procedure
  parse arg s
  return 'TEXT|'s

IsText: procedure
  parse arg v
  return GetShape(v) = 'TEXT'

/* number formatting for display: rounds to []PP decimal places (see
   the []PP quad variable), then trims needless trailing zeros */
Fmt: procedure
  parse arg n
  if pos('.', n) = 0 then return n
  pp = value('APLPP', , 'ENVIRONMENT')
  if pp = '' then pp = 6
  n = format(n, , pp)
  n = strip(n, 'T', '0')
  n = strip(n, 'T', '.')
  if n = '' | n = '-' then n = '0'
  return n

/* row-major -> multi-index helpers ------------------------------------ */

/* Strides(shp): returns space list of strides for row-major layout */
Strides: procedure
  parse arg shp
  if shp = '' then return ''
  n = words(shp)
  str. = 1
  do i = n-1 to 1 by -1
    str.i = str.(i+1) * word(shp, i+1)
  end
  s = ''
  do i = 1 to n
    s = s str.i
  end
  return strip(s)

/* LinearIndex(shp, idxlist) -> 1-based linear index into ravel, given a
   space-separated list of 1-based per-axis indices */
LinearIndex: procedure
  parse arg shp, idxs
  st = Strides(shp)
  n = words(shp)
  li = 1
  do i = 1 to n
    li = li + (word(idxs, i) - 1) * word(st, i)
  end
  return li

/* ======================================================================
   TOKENIZER
   Splits one source line into tokens.  Result is left in global stem
   TOK. (TOK.1 .. TOK.TOKN) via 'expose'.
   Multi-char operators recognised: <- <= >= <>
   A '-' immediately followed by a digit with no preceding space and not
   itself preceded by a value is read as a signed NUMBER token, e.g. -3.
   Comments: a line, or the remainder of a line, starting with '#'.
   ====================================================================== */

/* StripComment: removes a trailing "# ..." comment, but ignores any '#'
   that appears inside a 'quoted string' (with '' as an escaped quote) */
/* StripComment: removes a trailing comment, introduced by '#' or by the
   whole word "lamp" (this dialect's ASCII stand-in for APL's comment
   glyph '⍝', traditionally called "lamp"), whichever comes first --
   but ignores either marker if it appears inside a 'quoted string'
   (with '' as an escaped quote), and requires "lamp" to be a standalone
   word (not part of a longer identifier like "clamp" or "lampoon"). */
StripComment: procedure
  parse arg line
  len = length(line)
  inquote = 0
  i = 1
  do while i <= len
    c = substr(line, i, 1)
    if inquote then do
      if c = "'" then do
        if substr(line, i+1, 1) = "'" then i = i + 1   /* escaped '' */
        else inquote = 0
      end
    end
    else do
      if c = "'" then inquote = 1
      else if c = '#' then return left(line, i - 1)
      else if translate(substr(line, i, 4)) = 'LAMP' then do
        if i > 1 then prevc = substr(line, i - 1, 1)
        else prevc = ''
        nextc = substr(line, i + 4, 1)
        prevOK = (prevc = '') | (\datatype(prevc,'A') & \datatype(prevc,'N') & prevc \= '_')
        nextOK = (nextc = '') | (\datatype(nextc,'A') & \datatype(nextc,'N') & nextc \= '_')
        if prevOK & nextOK then return left(line, i - 1)
      end
    end
    i = i + 1
  end
  return line

Tokenize: procedure expose TOK. TOKN
  parse arg line
  drop TOK.
  TOKN = 0

  /* strip comment -- quote-aware, so a '#' inside 'a string' doesn't
     truncate the string */
  line = StripComment(line)
  line = strip(line)
  if line = '' then return

  i = 1
  len = length(line)
  do while i <= len
    c = substr(line, i, 1)
    if c = ' ' | c = '09'x then do
      i = i + 1
      iterate
    end
    /* quoted text literal: 'like this', with '' as an escaped quote */
    if c = "'" then do
      j = i + 1
      content = ''
      do forever
        if j > len then leave              /* unterminated: take rest of line */
        ch = substr(line, j, 1)
        if ch = "'" then do
          if substr(line, j+1, 1) = "'" then do
            content = content || "'"
            j = j + 2
            iterate
          end
          j = j + 1
          leave
        end
        content = content || ch
        j = j + 1
      end
      TOKN = TOKN + 1
      TOK.TOKN = '01'x || content     /* '01'x marks this token as text */
      i = j
      iterate
    end
    /* two-character operators */
    two = substr(line, i, 2)
    if two = '<-' | two = '<=' | two = '>=' | two = '<>' | two = '->' then do
      TOKN = TOKN + 1
      TOK.TOKN = two
      i = i + 2
      iterate
    end
    /* numbers (optionally signed) */
    if datatype(c, 'N') then do
      j = i
      do while j <= len & (datatype(substr(line,j,1),'N') | substr(line,j,1) = '.')
        j = j + 1
      end
      TOKN = TOKN + 1
      TOK.TOKN = substr(line, i, j - i)
      i = j
      iterate
    end
    if c = '-' & i < len & datatype(substr(line,i+1,1),'N') & \NumberPrecedes(TOKN) then do
      j = i + 1
      do while j <= len & (datatype(substr(line,j,1),'N') | substr(line,j,1) = '.')
        j = j + 1
      end
      TOKN = TOKN + 1
      TOK.TOKN = substr(line, i, j - i)
      i = j
      iterate
    end
    /* quad system variable/function tokens: [] immediately followed by
       a name, e.g. []IO []PP []TS -- kept together as ONE token so they
       read as a distinct kind of name from an ordinary identifier. */
    if c = '[' & substr(line, i+1, 1) = ']' then do
      j = i + 2
      do while j <= len & (datatype(substr(line,j,1),'A') | datatype(substr(line,j,1),'N'))
        j = j + 1
      end
      TOKN = TOKN + 1
      TOK.TOKN = substr(line, i, j - i)
      i = j
      iterate
    end
    /* single-character punctuation / operators */
    if pos(c, '()+-*/=<>,') > 0 then do
      TOKN = TOKN + 1
      TOK.TOKN = c
      i = i + 1
      iterate
    end
    /* identifier / word (letters, digits, underscore) */
    if datatype(c, 'A') | c = '_' then do
      j = i
      do while j <= len & (datatype(substr(line,j,1),'A') | datatype(substr(line,j,1),'N') | substr(line,j,1) = '_')
        j = j + 1
      end
      wordtok = substr(line, i, j - i)
      /* "quad" is a word-based synonym for the bare quad "[]" (used for
         explicit input/output, e.g. "quad<-expr" or plain "quad" to
         read a line of input) -- matching how this dialect spells other
         glyphs (nabla, edit, lamp) as words instead of symbols. */
      if translate(wordtok) = 'QUAD' then wordtok = '[]'
      TOKN = TOKN + 1
      TOK.TOKN = wordtok
      i = j
      iterate
    end
    /* unknown character - skip it */
    i = i + 1
  end
  return

/* True if the previous token was a value-ending token (number, ident,
   or closing paren) -- used to decide whether a following '-digit' is a
   signed literal (e.g. "3 -2" is two numbers) or should instead be a
   subtraction operator applied with no space (rare, we still favour
   literal reading which matches this dialect's documented rule: a
   minus glued to a digit is ALWAYS a signed number token). */
NumberPrecedes: procedure
  return 0

/* ======================================================================
   PARSER  (recursive-descent, right-associative -> right-to-left eval)

   Grammar:
     Stmt      := Ident '<-' Dyadic          (assignment, no output)
                | Dyadic                     (value, printed)
     Dyadic    := Monadic [ FuncTok Dyadic ]
     Monadic   := 'reduce' FuncName Monadic
                | 'scan'   FuncName Monadic
                | 'each'   FuncName Monadic
                | 'outer'  FuncName Operand Operand
                | FuncTok Monadic
                | Operand
     Operand   := '(' Dyadic ')'  |  Number+ (juxtaposed -> vector)  |  Ident

   Because Dyadic's right-hand side recurses into a full Dyadic parse,
   the rightmost sub-expression is parsed (and, since we build values
   directly rather than an AST, evaluated) first -- giving APL's
   right-to-left, no-precedence evaluation order for free.
   ====================================================================== */

/* word aliases for the arithmetic symbols, used as the "function name"
   argument to reduce / scan / each / outer, e.g.  total <- reduce plus v */
MapFuncWord: procedure
  parse arg w
  select
    when w = 'plus'   then return '+'
    when w = 'minus'  then return '-'
    when w = 'times'  then return '*'
    when w = 'divide' then return '/'
    otherwise return w
  end

IsFuncTok: procedure expose FUNC. USERFUNC.
  parse arg t
  if symbol('FUNC.t') = 'VAR' then return 1
  if USERFUNC.t = 1 then return 1
  return 0

IsUserFunc: procedure expose USERFUNC.
  parse arg t
  return USERFUNC.t = 1

IsNiladicUserFunc: procedure expose FNHDR.
  parse arg t
  hdr = FNHDR.t
  parse var hdr arity '|' rest
  return arity = 0

ArityWord: procedure
  parse arg n
  if n = 0 then return 'niladic'
  if n = 1 then return 'monadic'
  return 'dyadic'

IsQuadTok: procedure
  parse arg t
  if left(t, 2) = '[]' then return 1
  return 0

/* QuadGet: read a quad system variable/function's current value */
QuadGet: procedure expose TOK. TOKN POS VARS. VARSET. VARLIST FUNC. USERFUNC. FNHDR. FNBODY. FNLINECOUNT.
  parse arg t
  nm = translate(substr(t, 3))
  select
    when nm = '' then return QuadInput()
    when nm = 'IO' then return MkVal('', value('APLIO', , 'ENVIRONMENT'))
    when nm = 'PP' then return MkVal('', value('APLPP', , 'ENVIRONMENT'))
    when nm = 'TS' then do
      d = date('S'); t2 = time('N')
      parse var t2 hh ':' mm ':' ss
      rav = left(d,4)+0 (substr(d,5,2)+0) (substr(d,7,2)+0) (hh+0) (mm+0) (ss+0)
      return MkVal(6, rav)
    end
    otherwise return RunErr('unknown system variable:' t)
  end

/* QuadInput -- real APL's ⎕ (quad) read: reading the bare quad as a
   VALUE prompts for a line of input, then evaluates it as a single
   statement, returning its result. This is genuine terminal input, so
   it always reads from the terminal (standard input) regardless of
   whether the surrounding code came from the REPL or a loaded script,
   the same way an interactive prompt would in a real APL session. */
QuadInput: procedure expose TOK. TOKN POS VARS. VARSET. VARLIST FUNC. USERFUNC. FNHDR. FNBODY. FNLINECOUNT.
  call charout , '      '        /* real APL indents a quad-input prompt */
  if lines() <= 0 then return RunErr('[] (quad) input: no more input available')
  ln = linein()
  call Tokenize ln
  if TOKN = 0 then return RunErr('[] (quad) input: empty input')
  return ParseStmt()

/* QuadSet: assign to a settable quad system variable -- or, for the
   bare quad "[]" (no name, nm=''), display the value immediately, the
   same shorthand real APL's ⎕<-value provides for explicit output. */
QuadSet: procedure
  parse arg t, val
  nm = translate(substr(t, 3))
  if nm = '' then do
    call PrintVal val
    return ''
  end
  if \IsScalar(val) & \IsText(val) then return RunErr(t 'must be assigned a scalar')
  select
    when nm = 'IO' then do
      n = GetRavel(val)
      if n <> 0 & n <> 1 then return RunErr('[]IO must be 0 or 1')
      call value 'APLIO', n, 'ENVIRONMENT'
    end
    when nm = 'PP' then do
      n = GetRavel(val)
      if n < 0 | n <> trunc(n) then return RunErr('[]PP must be a non-negative integer')
      call value 'APLPP', n, 'ENVIRONMENT'
    end
    when nm = 'TS' then return RunErr('[]TS is read-only')
    otherwise return RunErr('unknown system variable:' t)
  end
  return ''

ParseStmt: procedure expose TOK. TOKN POS VARS. VARSET. VARLIST FUNC. USERFUNC. FNHDR. FNBODY. FNLINECOUNT.
  POS = 1
  if TOKN >= 1 & TOK.1 = '->' then do
    POS = 2
    if POS > TOKN then do
      /* bare "->" : unconditional exit from the current function */
      call value 'APLBRANCH', 'EXIT', 'ENVIRONMENT'
      return ''
    end
    val = ParseDyadic()
    if POS <= TOKN then return ParseErr('unexpected token near' TOK.POS)
    rav = GetRavel(val)
    if strip(rav) = '' then return ''    /* branch to empty: no branch, fall through */
    call value 'APLBRANCH', word(rav, 1), 'ENVIRONMENT'
    return ''
  end
  if TOKN >= 2 then do
    if TOK.2 = '<-' & IsQuadTok(TOK.1) then do
      qname = TOK.1
      POS = 3
      val = ParseDyadic()
      if POS <= TOKN then return ParseErr('unexpected token near' TOK.POS)
      call QuadSet qname, val
      return ''
    end
    if TOK.2 = '<-' & \IsFuncTok(TOK.1) & \IsReserved(TOK.1) then do
      vname = TOK.1
      POS = 3
      val = ParseDyadic()
      if POS <= TOKN then return ParseErr('unexpected token near' TOK.POS)
      VARS.vname = val
      if \VARSET.vname then VARLIST = VARLIST vname
      VARSET.vname = 1
      return ''            /* assignment: no display */
    end
  end
  val = ParseDyadic()
  if POS <= TOKN then return ParseErr('unexpected token near' TOK.POS)
  return val

IsReserved: procedure
  parse arg t
  if wordpos(t, 'reduce scan each outer') > 0 then return 1
  return 0

ParseDyadic: procedure expose TOK. TOKN POS VARS. VARSET. FUNC. USERFUNC. FNHDR. FNBODY. FNLINECOUNT. VARLIST
  left = ParseMonadic()
  if POS <= TOKN then do
    if IsFuncTok(TOK.POS) then do
      f = TOK.POS
      POS = POS + 1
      right = ParseDyadic()
      return ApplyDyadic(f, left, right)
    end
  end
  return left

ParseMonadic: procedure expose TOK. TOKN POS VARS. VARSET. FUNC. USERFUNC. FNHDR. FNBODY. FNLINECOUNT. VARLIST
  if POS > TOKN then return ParseErr('unexpected end of statement')
  t = TOK.POS
  select
    when t = 'reduce' then do
      POS = POS + 1
      fn = MapFuncWord(TOK.POS); POS = POS + 1
      arr = ParseMonadic()
      return ApplyReduce(fn, arr)
    end
    when t = 'scan' then do
      POS = POS + 1
      fn = MapFuncWord(TOK.POS); POS = POS + 1
      arr = ParseMonadic()
      return ApplyScan(fn, arr)
    end
    when t = 'each' then do
      POS = POS + 1
      fn = MapFuncWord(TOK.POS); POS = POS + 1
      arr = ParseMonadic()
      return ApplyEach(fn, arr)
    end
    when t = 'outer' then do
      POS = POS + 1
      fn = MapFuncWord(TOK.POS); POS = POS + 1
      a = ParseOperand()
      b = ParseOperand()
      return ApplyOuter(fn, a, b)
    end
    when USERFUNC.t = 1 & IsNiladicUserFunc(t) then do
      POS = POS + 1
      return CallUserFunc(t, 0, '', '')
    end
    when IsFuncTok(t) then do
      POS = POS + 1
      arg1 = ParseMonadic()
      return ApplyMonadic(t, arg1)
    end
    otherwise
      return ParseOperand()
  end

ParseOperand: procedure expose TOK. TOKN POS VARS. VARSET. FUNC. USERFUNC. FNHDR. FNBODY. FNLINECOUNT. VARLIST
  if POS > TOKN then return ParseErr('unexpected end of statement')
  t = TOK.POS
  if left(t, 1) = '01'x then do
    POS = POS + 1
    return MkText(substr(t, 2))
  end
  if t = '(' then do
    POS = POS + 1
    v = ParseDyadic()
    if POS <= TOKN & TOK.POS = ')' then POS = POS + 1
    else return ParseErr('expected )')
    return v
  end
  if datatype(t, 'N') then do
    nums = t
    POS = POS + 1
    do while POS <= TOKN & datatype(TOK.POS, 'N')
      nums = nums TOK.POS
      POS = POS + 1
    end
    if words(nums) = 1 then return MkVal('', nums)
    return MkVal(words(nums), nums)
  end
  if IsQuadTok(t) then do
    POS = POS + 1
    return QuadGet(t)
  end
  /* identifier */
  POS = POS + 1
  if USERFUNC.t = 1 then return CallUserFunc(t, 0, '', '')   /* niladic call */
  if \ VARSET.t then return ParseErr('undefined variable:' t)
  return VARS.t

ParseErr: procedure
  parse arg msg
  call FlagErr 'apl: syntax error --' msg
  return '|0'

/* ======================================================================
   RUNTIME ERROR
   ====================================================================== */
RunErr: procedure
  parse arg msg
  call FlagErr 'apl: runtime error --' msg
  return '|0'

/* FlagErr: prints the message (once per statement) and raises the
   process-wide APLERR flag via Regina's ENVIRONMENT variable pool, which
   -- unlike ordinary REXX variables -- is visible everywhere regardless
   of PROCEDURE/EXPOSE scoping.  Callers of RunErr/ParseErr simply RETURN
   a harmless dummy value ('|0', i.e. scalar zero) instead of aborting;
   that dummy value is allowed to propagate harmlessly up through the
   rest of the (already-doomed) expression evaluation, and the one place
   that actually matters -- RunFile / RunRepl, right after ParseStmt()
   returns -- checks the flag once and discards the whole statement's
   result. This avoids ever needing a non-local jump (SIGNAL) out of
   deeply-nested, mostly-isolated PROCEDUREs, which turned out to leave
   Regina's call-frame bookkeeping in a bad state for later, unrelated
   RETURNs. */
FlagErr: procedure
  parse arg msg
  cur = value('APLERR', , 'ENVIRONMENT')
  if cur = '' then do
    if value('APLSUPPRESSERR', , 'ENVIRONMENT') \= '1' then say msg
    call value 'APLERR', msg, 'ENVIRONMENT'
  end
  return

/* ======================================================================
   USER-DEFINED FUNCTIONS  ( nabla / edit  --  the classic APL "del"
   function editor, see regina_apl_doc.md for the full write-up )

   Storage:
     FNHDR.name        = "arity|resultvar|paramlist|locallist"
                          arity: 0/1/2 ; resultvar: '-' if none
     FNBODY.name.i      = text of body line i  (i = 1..FNLINECOUNT.name,
                          permanent storage, always consecutive integers)
     FNLINECOUNT.name  = number of permanent body lines
     USERFUNC.name     = 1 once the function is defined
     FNLIST            = space-separated list of all function names ever
                          defined (mirrors VARLIST; )FNS filters on
                          USERFUNC. the same way )VARS filters on VARSET.)
   ====================================================================== */

/* FormatHeader: reconstructs the header text for display, e.g. "Z<-FOO X" */
FormatHeader: procedure
  parse arg fname, arity, resultvar, paramlist
  select
    when arity = 0 then rhs = fname
    when arity = 1 then rhs = fname word(paramlist, 1)
    otherwise rhs = word(paramlist, 1) fname word(paramlist, 2)
  end
  if resultvar = '-' then return rhs
  return resultvar'<-'rhs

/* HeaderParse: parses the text typed after "nabla"/"edit" into
   "arity|resultvar|paramlist|locallist|fname", or returns '' (having
   already printed a message) if the header is unacceptable. */
HeaderParse: procedure expose FUNC. USERFUNC. VARSET.
  parse arg text
  text = strip(text)
  if text = '' then do
    say 'usage: nabla <header>   e.g. nabla Z<-FOO X',
        '   (or: nabla FOONAME to edit an existing function)'
    return ''
  end
  /* split off ;LOCAL1;LOCAL2... */
  semipos = pos(';', text)
  if semipos > 0 then do
    hdrpart = strip(left(text, semipos - 1))
    rest = substr(text, semipos + 1)
    locallist = ''
    do while rest <> ''
      sp = pos(';', rest)
      if sp = 0 then do
        locallist = locallist strip(rest)
        rest = ''
      end
      else do
        locallist = locallist strip(left(rest, sp - 1))
        rest = substr(rest, sp + 1)
      end
    end
    locallist = strip(locallist)
  end
  else do
    hdrpart = text
    locallist = ''
  end
  /* split off RESULT<- */
  arrowpos = pos('<-', hdrpart)
  if arrowpos > 0 then do
    resultvar = strip(left(hdrpart, arrowpos - 1))
    rhs = strip(substr(hdrpart, arrowpos + 2))
    if resultvar = '' | words(resultvar) <> 1 then do
      say 'apl: bad function header (expected a single result-variable name before <-)'
      return ''
    end
  end
  else do
    resultvar = '-'
    rhs = hdrpart
  end
  nw = words(rhs)
  select
    when nw = 1 then do
      arity = 0; fname = word(rhs, 1); paramlist = ''
    end
    when nw = 2 then do
      arity = 1; fname = word(rhs, 1); paramlist = word(rhs, 2)
    end
    when nw = 3 then do
      arity = 2; fname = word(rhs, 2); paramlist = word(rhs, 1) word(rhs, 3)
    end
    otherwise do
      say 'apl: bad function header:' text
      return ''
    end
  end
  ucfname = translate(fname)
  if wordpos(ucfname, 'NABLA EDIT') > 0 then do
    say 'apl:' fname 'is reserved and cannot be a function name'
    return ''
  end
  if IsFuncTok(fname) & USERFUNC.fname \= 1 then do
    say 'apl:' fname 'is a built-in primitive and cannot be redefined'
    return ''
  end
  if VARSET.fname then do
    say 'apl:' fname 'is already a variable; clear it first, or pick another name'
    return ''
  end
  /* A function's own namespace (result var, parameters, locals) must
     not collide with the function's own name, or with any OTHER
     existing function/primitive -- because variable and function names
     share one namespace here, exactly as in real APL. Without this
     check, a body line like "sum<-sum x" is ambiguous: is "sum<-"
     assigning to the local result, or is "sum" being invoked again?
     The parser resolves that ambiguity in favor of "it's a function
     call" (since a registered function name is never a valid
     assignment target), which silently breaks the assignment and can
     even recurse without end. Rejecting the collision up front avoids
     the whole problem. */
  localnames = paramlist
  if resultvar \= '-' then localnames = localnames resultvar
  localnames = strip(localnames locallist)
  do i = 1 to words(localnames)
    nm = word(localnames, i)
    if nm = fname then do
      say 'apl:' nm 'is both this function''s own name and one of its',
          'parameters/result/locals -- pick a different name for one of them'
      return ''
    end
    if IsFuncTok(nm) then do
      say 'apl:' nm 'is already a function or primitive name -- pick a',
          'different parameter/result/local name'
      return ''
    end
  end
  return arity'|'resultvar'|'paramlist'|'locallist'|'fname

/* FmtKey: display form of a (possibly fractional) line-number key --
   trailing zeros in the fractional part are not shown, matching the
   original line editor's convention. */
FmtKey: procedure
  parse arg k
  if pos('.', k) = 0 then return k
  k = strip(k, 'T', '0')
  k = strip(k, 'T', '.')
  return k

/* IncrementUnit: after positioning at line-number key k, the *next*
   auto-numbered prompt advances by "a unit of the display form of the
   last decimal digit" of k -- i.e. by 1 for a plain integer, by 0.1
   after a one-decimal key like 2.3, by 0.01 after a two-decimal key,
   and so on. This is the exact rule documented for the original APL
   line editor's fractional line numbering. */
IncrementUnit: procedure
  parse arg k
  dp = pos('.', k)
  if dp = 0 then return 1
  decimals = length(k) - dp
  if decimals <= 0 then return 1
  return 1 / (10 ** decimals)

/* ======================================================================
   LABELS  ( for  ->  branching inside a function body )
   A label is written "NAME:" tight against the start of a line (no
   space between the name and the colon), e.g. "LOOP: Z<-Z+1", or just
   "LOOP:" alone on a line that does nothing but mark a branch target.
   ====================================================================== */

/* GetLineLabel: returns the label name at the start of txt, or '' if
   the line doesn't begin with one */
GetLineLabel: procedure
  parse arg txt
  txt = strip(txt)
  if txt = '' then return ''
  if \datatype(substr(txt, 1, 1), 'A') then return ''
  len = length(txt)
  i = 1
  do while i <= len & (datatype(substr(txt,i,1),'A') | datatype(substr(txt,i,1),'N') | substr(txt,i,1) = '_')
    i = i + 1
  end
  if i > len then return ''
  if substr(txt, i, 1) <> ':' then return ''
  return left(txt, i - 1)

/* StripLabelPrefix: removes a leading "NAME:" label marker (if any),
   returning just the executable remainder of the line */
StripLabelPrefix: procedure
  parse arg txt
  txt = strip(txt)
  lbl = GetLineLabel(txt)
  if lbl = '' then return txt
  return strip(substr(txt, length(lbl) + 2))

/* ======================================================================
   EnterDefineMode -- the nabla/edit line editor itself.
   headerOrName: text typed after "nabla"/"edit" on the triggering line.
   srcstream:    '' to read further lines from the terminal (REPL), or
                 an open file name to read them from a script instead.
   ====================================================================== */
EnterDefineMode: procedure expose FUNC. USERFUNC. VARSET. FNHDR. FNBODY.,
                                   FNLINECOUNT. FNLIST
  parse arg headerOrName, srcstream
  ht = strip(headerOrName)
  if ht = '' then do
    say 'usage: nabla <header>   e.g. nabla Z<-FOO X',
        '   (or: nabla FOONAME to edit an existing function,',
        '    or: nabla FOONAME nabla  to just display it)'
    return
  end

  /* "nabla FOONAME nabla" (or "edit") on one line: display only, don't
     enter the editor at all -- matches the original editor's documented
     shorthand for "open and immediately close with no changes" */
  if words(ht) = 2 then do
    cand = word(ht, 1)
    closer = translate(word(ht, 2))
    if (closer = 'NABLA' | closer = 'EDIT') & USERFUNC.cand = 1 then do
      hdr = FNHDR.cand
      parse var hdr arity '|' resultvar '|' paramlist '|' locallist
      say '     'FormatHeader(cand, arity, resultvar, paramlist)
      do i = 1 to FNLINECOUNT.cand
        say '['i']  'FNBODY.cand.i
      end
      return
    end
  end

  editingExisting = 0
  if words(ht) = 1 then do
    if USERFUNC.ht = 1 then editingExisting = 1
  end

  if editingExisting then do
    fname = ht
    hdr = FNHDR.fname
    parse var hdr arity '|' resultvar '|' paramlist '|' locallist
    stagecount = FNLINECOUNT.fname
    say '     'FormatHeader(fname, arity, resultvar, paramlist)
    do i = 1 to stagecount
      stagekey.i = i
      stagetext.i = FNBODY.fname.i
      say '['i']  'stagetext.i
    end
    nextkey = stagecount + 1
    lastincrement = 1
  end
  else do
    parsed = HeaderParse(ht)
    if parsed = '' then return
    parse var parsed arity '|' resultvar '|' paramlist '|' locallist '|' fname
    say '     'FormatHeader(fname, arity, resultvar, paramlist)
    stagecount = 0
    nextkey = 1
    lastincrement = 1
  end

  do forever
    if srcstream = '' then do
      call charout , '['FmtKey(nextkey)']  '
      if lines() <= 0 then leave
      ln = linein()
    end
    else do
      if lines(srcstream) <= 0 then leave
      ln = linein(srcstream)
    end
    lnstrip = strip(ln)
    ucln = translate(lnstrip)
    if ucln = 'NABLA' | ucln = 'EDIT' then leave

    if lnstrip = '[]' then do
      say '     'FormatHeader(fname, arity, resultvar, paramlist)
      do i = 1 to stagecount
        say '['FmtKey(stagekey.i)']  'stagetext.i
      end
      iterate
    end

    if left(lnstrip, 1) = '[' then do
      cp = pos(']', lnstrip)
      isDirective = 0
      if cp > 2 then do
        candidate = substr(lnstrip, 2, cp - 2)
        testkey = candidate
        if left(testkey, 1) = '-' then testkey = substr(testkey, 2)
        if datatype(strip(testkey), 'N') then isDirective = 1
      end
    end
    else isDirective = 0

    if isDirective then do
      keytxt = strip(candidate)
      rest = strip(substr(lnstrip, cp + 1))
      isDelete = 0
      if left(keytxt, 1) = '-' then do
        isDelete = 1
        keytxt = strip(substr(keytxt, 2))
      end
      if keytxt <= 0 then do
        say 'apl: bad line number:' keytxt
        iterate
      end

      if isDelete then do
        found = 0
        do i = 1 to stagecount
          if stagekey.i = keytxt then do
            found = i
            leave
          end
        end
        if found = 0 then say 'apl: no such line:' keytxt
        else do
          do j = found to stagecount - 1
            jj = j + 1
            stagekey.j = stagekey.jj
            stagetext.j = stagetext.jj
          end
          stagecount = stagecount - 1
        end
        iterate
      end

      if rest = '' then do
        found = 0
        do i = 1 to stagecount
          if stagekey.i = keytxt then do
            say '['FmtKey(stagekey.i)']  'stagetext.i
            found = 1
            leave
          end
        end
        if \found then say 'apl: no such line:' keytxt
        nextkey = keytxt
        lastincrement = IncrementUnit(keytxt)
        iterate
      end

      /* replace-or-insert at keytxt */
      found = 0
      do i = 1 to stagecount
        if stagekey.i = keytxt then do
          stagetext.i = rest
          found = 1
          leave
        end
      end
      if \found then do
        inspos = stagecount + 1
        do i = 1 to stagecount
          if stagekey.i > keytxt then do
            inspos = i
            leave
          end
        end
        do j = stagecount to inspos by -1
          jj = j + 1
          stagekey.jj = stagekey.j
          stagetext.jj = stagetext.j
        end
        stagekey.inspos = keytxt
        stagetext.inspos = rest
        stagecount = stagecount + 1
      end
      nextkey = keytxt
      lastincrement = IncrementUnit(keytxt)
      iterate
    end

    /* plain text: append at the current auto-numbered key */
    stagecount = stagecount + 1
    stagekey.stagecount = nextkey
    stagetext.stagecount = ln
    nextkey = nextkey + lastincrement
  end

  /* close: renumber to consecutive integers 1..N and commit permanently */
  do i = 1 to stagecount
    FNBODY.fname.i = stagetext.i
  end
  FNLINECOUNT.fname = stagecount
  FNHDR.fname = arity'|'resultvar'|'paramlist'|'locallist
  if USERFUNC.fname \= 1 then FNLIST = FNLIST fname
  USERFUNC.fname = 1
  say '     'fname 'defined.'
  return

/* ======================================================================
   CallUserFunc -- invoke a user-defined function.
   nargs: 0 = niladic call, 1 = monadic, 2 = dyadic.
   Parameters, the result variable, and any declared locals are
   "localized": their prior global value (if any) is saved and restored
   around the call, exactly like the original APL's dynamic scoping --
   which is also what makes straightforward recursion work correctly.
   ====================================================================== */
CallUserFunc: procedure expose VARS. VARSET. VARLIST FUNC. USERFUNC. FNHDR. FNBODY. FNLINECOUNT.
  parse arg fname, nargs, leftval, rightval
  hdr = FNHDR.fname
  parse var hdr arity '|' resultvar '|' paramlist '|' locallist
  if arity <> nargs then
    return RunErr(fname 'is' ArityWord(arity)', but was called as if' ArityWord(nargs))

  n = FNLINECOUNT.fname

  /* scan for labels ("NAME:" at the start of a line) so that ->NAME
     works; each label found becomes an extra localized name bound to
     its line number, exactly like a parameter or declared local */
  labellist = ''
  do i = 1 to n
    lbl = GetLineLabel(FNBODY.fname.i)
    if lbl <> '' then do
      labelline.lbl = i
      labellist = labellist lbl
    end
  end
  labellist = strip(labellist)

  localnames = paramlist
  if resultvar <> '-' then localnames = localnames resultvar
  localnames = strip(localnames locallist labellist)

  depth = value('APLCALLDEPTH', , 'ENVIRONMENT') + 1
  call value 'APLCALLDEPTH', depth, 'ENVIRONMENT'

  do i = 1 to words(localnames)
    nm = word(localnames, i)
    call value 'FSAVESET_'depth'_'nm, VARSET.nm, 'ENVIRONMENT'
    if VARSET.nm then call value 'FSAVEVAL_'depth'_'nm, VARS.nm, 'ENVIRONMENT'
    VARSET.nm = 0
    drop VARS.nm
  end

  if arity >= 1 then do
    pname = word(paramlist, 1)
    VARS.pname = rightval
    VARSET.pname = 1
  end
  if arity = 2 then do
    pname2 = word(paramlist, 2)
    VARS.pname2 = leftval
    VARSET.pname2 = 1
  end
  do i = 1 to words(labellist)
    lbl = word(labellist, i)
    VARS.lbl = MkVal('', labelline.lbl)
    VARSET.lbl = 1
  end

  call value 'APLERR', '', 'ENVIRONMENT'
  call value 'APLBRANCH', '', 'ENVIRONMENT'
  curline = 1
  do while curline <= n
    call Tokenize StripLabelPrefix(FNBODY.fname.curline)
    if TOKN > 0 then do
      result = ParseStmt()
      if value('APLERR', , 'ENVIRONMENT') <> '' then leave
      if result <> '' then call PrintVal result
      branch = value('APLBRANCH', , 'ENVIRONMENT')
      if branch <> '' then do
        call value 'APLBRANCH', '', 'ENVIRONMENT'
        if branch = 'EXIT' then leave
        if datatype(branch, 'W') & branch >= 1 & branch <= n then curline = branch
        else leave   /* out-of-range target: end the function, as real APL does */
        iterate
      end
    end
    curline = curline + 1
  end

  haderror = (value('APLERR', , 'ENVIRONMENT') <> '')
  if haderror then callresult = '|0'
  else if resultvar <> '-' then do
    if VARSET.resultvar then callresult = VARS.resultvar
    else callresult = RunErr(fname 'did not set a result ('resultvar' was never assigned)')
  end
  else callresult = ''

  do i = 1 to words(localnames)
    nm = word(localnames, i)
    wasset = value('FSAVESET_'depth'_'nm, , 'ENVIRONMENT')
    if wasset = 1 then do
      VARS.nm = value('FSAVEVAL_'depth'_'nm, , 'ENVIRONMENT')
      VARSET.nm = 1
    end
    else do
      VARSET.nm = 0
      drop VARS.nm
    end
  end
  call value 'APLCALLDEPTH', depth - 1, 'ENVIRONMENT'

  return callresult

/* ======================================================================
   ELEMENTWISE SCALAR OPS (with scalar-extension broadcasting)
   ====================================================================== */

ScalarBin: procedure
  parse arg f, x, y
  select
    when f = '+'   then return x + y
    when f = '-'   then return x - y
    when f = '*'   then return x * y
    when f = '/'   then do
      if y = 0 then return RunErr('divide by zero')
      return x / y
    end
    when f = '='   then if x = y then return 1; else return 0
    when f = '<'   then if x < y then return 1; else return 0
    when f = '>'   then if x > y then return 1; else return 0
    when f = '<='  then if x <= y then return 1; else return 0
    when f = '>='  then if x >= y then return 1; else return 0
    when f = '<>'  then if x <> y then return 1; else return 0
    when f = 'max' then if x > y then return x; else return y
    when f = 'min' then if x < y then return x; else return y
    when f = 'and' then if x = 1 & y = 1 then return 1; else return 0
    when f = 'or'  then if x = 1 | y = 1 then return 1; else return 0
    when f = 'mod' then do
      if y = 0 then return RunErr('mod by zero')
      return x // y
    end
    otherwise return RunErr('unknown dyadic scalar function:' f)
  end

ScalarUn: procedure
  parse arg f, x
  select
    when f = '+'        then return x
    when f = '-'         then return -x
    when f = 'neg'       then return -x
    when f = 'not'       then do
      if x = 0 then return 1; else return 0
    end
    when f = 'abs'       then return abs(x)
    when f = 'floor'     then return trunc(x) - (x < 0 & x <> trunc(x))
    when f = 'ceil'      then return trunc(x) + (x > 0 & x <> trunc(x))
    otherwise return RunErr('unknown monadic scalar function:' f)
  end

/* is f one of the "scalar" (elementwise, broadcasting) primitives? */
IsScalarDyadic: procedure
  parse arg f
  return wordpos(f, '+ - * / = < > <= >= <> max min and or mod') > 0

IsScalarMonadic: procedure
  parse arg f
  return wordpos(f, '+ - neg not abs floor ceil') > 0

/* ElementwiseDyadic: applies a scalar dyadic op with broadcasting */
ElementwiseDyadic: procedure
  parse arg f, x, y
  sx = GetShape(x); sy = GetShape(y)
  rx = GetRavel(x);  ry = GetRavel(y)
  if sx = '' then do                       /* scalar op array */
    n = NumEl(sy)
    out = ''
    do i = 1 to n
      out = out ScalarBin(f, rx, word(ry, i))
    end
    return MkVal(sy, strip(out))
  end
  if sy = '' then do                       /* array op scalar */
    n = NumEl(sx)
    out = ''
    do i = 1 to n
      out = out ScalarBin(f, word(rx, i), ry)
    end
    return MkVal(sx, strip(out))
  end
  if sx <> sy then return RunErr('shape mismatch:' sx '/' sy)
  n = NumEl(sx)
  out = ''
  do i = 1 to n
    out = out ScalarBin(f, word(rx, i), word(ry, i))
  end
  return MkVal(sx, strip(out))

ElementwiseMonadic: procedure
  parse arg f, x
  sx = GetShape(x); rx = GetRavel(x)
  n = NumEl(sx)
  out = ''
  do i = 1 to n
    out = out ScalarUn(f, word(rx, i))
  end
  return MkVal(sx, strip(out))

/* ======================================================================
   MONADIC DISPATCH
   ====================================================================== */
ApplyMonadic: procedure expose VARS. VARSET. VARLIST FUNC. USERFUNC. FNHDR. FNBODY. FNLINECOUNT.
  parse arg f, x
  if USERFUNC.f = 1 then return CallUserFunc(f, 1, '', x)
  if IsText(x) then return RunErr('DOMAIN ERROR:' f 'does not accept a text value')
  if IsScalarMonadic(f) then return ElementwiseMonadic(f, x)
  select
    when f = 'iota'      then return PrimIota(x)
    when f = 'rho'       then return PrimShapeOf(x)
    when f = 'ravel'     then return MkVal(NumEl(GetShape(x)), GetRavel(x))
    when f = 'reverse'   then return PrimReverse(x)
    when f = 'transpose' then return PrimTranspose(x)
    otherwise return RunErr(f 'cannot be used monadically')
  end

PrimIota: procedure
  parse arg x
  if \IsScalar(x) then return RunErr('iota requires a scalar argument')
  n = GetRavel(x)
  if n < 0 | n <> trunc(n) then return RunErr('iota requires a non-negative integer')
  io = value('APLIO', , 'ENVIRONMENT')
  if io = '' then io = 1
  rav = ''
  do i = io to io + n - 1
    rav = rav i
  end
  return MkVal(n, strip(rav))

PrimShapeOf: procedure
  shp = GetShape(arg(1))
  if shp = '' then return MkVal(0, '')
  return MkVal(words(shp), shp)

PrimReverse: procedure
  x = arg(1)
  shp = GetShape(x); rav = GetRavel(x)
  r = Rank(x)
  if r <= 1 then do
    out = ''
    do i = words(rav) to 1 by -1
      out = out word(rav, i)
    end
    return MkVal(shp, strip(out))
  end
  if r = 2 then do
    rows = word(shp,1); cols = word(shp,2)
    out = ''
    do i = 1 to rows
      do j = cols to 1 by -1
        out = out word(rav, (i-1)*cols + j)
      end
    end
    return MkVal(shp, strip(out))
  end
  return RunErr('reverse: rank > 2 not supported')

PrimTranspose: procedure
  x = arg(1)
  r = Rank(x)
  if r < 2 then return x
  shp = GetShape(x); rav = GetRavel(x)
  rows = word(shp,1); cols = word(shp,2)
  newshp = cols rows
  out = ''
  do j = 1 to cols
    do i = 1 to rows
      out = out word(rav, (i-1)*cols + j)
    end
  end
  return MkVal(newshp, strip(out))

/* ======================================================================
   DYADIC DISPATCH
   ====================================================================== */
ApplyDyadic: procedure expose VARS. VARSET. VARLIST FUNC. USERFUNC. FNHDR. FNBODY. FNLINECOUNT.
  parse arg f, x, y
  if USERFUNC.f = 1 then return CallUserFunc(f, 2, x, y)
  if f = '[]EA' then return PrimExecuteAlternate(x, y)
  if f = 'cat' & IsText(x) & IsText(y) then return MkText(GetRavel(x) || GetRavel(y))
  if IsText(x) | IsText(y) then return RunErr('DOMAIN ERROR:' f 'does not accept a text value')
  if IsScalarDyadic(f) then return ElementwiseDyadic(f, x, y)
  select
    when f = 'rho'       then return PrimReshape(x, y)
    when f = 'take'      then return PrimTake(x, y)
    when f = 'drop'      then return PrimDrop(x, y)
    when f = 'rotate'    then return PrimRotate(x, y)
    when f = 'cat'       then return PrimCat(x, y)
    when f = 'compress'  then return PrimCompress(x, y)
    when f = 'index'     then return PrimIndex(x, y)
    when f = 'matmul'    then return PrimMatmul(x, y)
    otherwise return RunErr(f 'cannot be used dyadically')
  end

/* Y = shape descriptor (scalar or vector), X = data to (cyclically) fill */
PrimReshape: procedure
  parse arg y, x
  newshp = strip(GetRavel(y))
  total = NumEl(newshp)
  rx = GetRavel(x)
  nx = words(rx)
  if nx = 0 then return RunErr('reshape: empty data')
  out = ''
  do i = 0 to total - 1
    out = out word(rx, (i // nx) + 1)
  end
  return MkVal(newshp, strip(out))

/* count TAKE x  -- count scalar (vector) or 2-vector (matrix) */
PrimTake: procedure
  parse arg cnt, x
  r = Rank(x)
  shp = GetShape(x); rav = GetRavel(x)
  if r <= 1 then do
    if \IsScalar(cnt) then return RunErr('take: count must be scalar for a vector')
    n = GetRavel(cnt)
    len = words(rav)
    out = ''
    if n >= 0 then do
      do i = 1 to n
        if i <= len then out = out word(rav, i); else out = out 0
      end
    end
    else do
      n = abs(n)
      start = len - n + 1
      do i = start to len
        if i >= 1 then out = out word(rav, i); else out = out 0
      end
    end
    return MkVal(n, strip(out))
  end
  if r = 2 then do
    if Rank(cnt) <> 1 | words(GetRavel(cnt)) <> 2 then
      return RunErr('take: count must be a 2-element vector for a matrix')
    cr = word(GetRavel(cnt),1); cc = word(GetRavel(cnt),2)
    rows = word(shp,1); cols = word(shp,2)
    nr = abs(cr); nc = abs(cc)
    out = ''
    do ii = 1 to nr
      if cr >= 0 then srow = ii; else srow = rows - nr + ii
      do jj = 1 to nc
        if cc >= 0 then scol = jj; else scol = cols - nc + jj
        if srow >= 1 & srow <= rows & scol >= 1 & scol <= cols then
          out = out word(rav, (srow-1)*cols + scol)
        else
          out = out 0
      end
    end
    return MkVal(nr nc, strip(out))
  end
  return RunErr('take: rank > 2 not supported')

/* count DROP x -- inverse companion of take */
PrimDrop: procedure
  parse arg cnt, x
  r = Rank(x)
  shp = GetShape(x); rav = GetRavel(x)
  if r <= 1 then do
    if \IsScalar(cnt) then return RunErr('drop: count must be scalar for a vector')
    n = GetRavel(cnt)
    len = words(rav)
    if n >= 0 then do
      keep = max(len - n, 0)
      out = ''
      do i = n+1 to len
        out = out word(rav, i)
      end
    end
    else do
      n = abs(n)
      keep = max(len - n, 0)
      out = ''
      do i = 1 to keep
        out = out word(rav, i)
      end
    end
    return MkVal(keep, strip(out))
  end
  if r = 2 then do
    if Rank(cnt) <> 1 | words(GetRavel(cnt)) <> 2 then
      return RunErr('drop: count must be a 2-element vector for a matrix')
    cr = word(GetRavel(cnt),1); cc = word(GetRavel(cnt),2)
    rows = word(shp,1); cols = word(shp,2)
    nr = max(rows - abs(cr), 0); nc = max(cols - abs(cc), 0)
    if cr >= 0 then rstart = cr + 1; else rstart = 1
    if cc >= 0 then cstart = cc + 1; else cstart = 1
    out = ''
    do ii = 0 to nr - 1
      do jj = 0 to nc - 1
        out = out word(rav, (rstart+ii-1)*cols + (cstart+jj))
      end
    end
    return MkVal(nr nc, strip(out))
  end
  return RunErr('drop: rank > 2 not supported')

/* n ROTATE x : positive -> rotate left (vector), or per-row for matrix */
PrimRotate: procedure
  parse arg n, x
  r = Rank(x)
  shp = GetShape(x); rav = GetRavel(x)
  if r <= 1 then do
    if \IsScalar(n) then return RunErr('rotate: shift must be scalar for a vector')
    len = words(rav)
    if len = 0 then return x
    k = GetRavel(n) // len
    if k < 0 then k = k + len
    out = ''
    do i = 1 to len
      src = ((i - 1 + k) // len) + 1
      out = out word(rav, src)
    end
    return MkVal(shp, strip(out))
  end
  if r = 2 then do
    if \IsScalar(n) then return RunErr('rotate: only scalar shift supported for matrices')
    rows = word(shp,1); cols = word(shp,2)
    k = GetRavel(n) // cols
    if k < 0 then k = k + cols
    out = ''
    do i = 1 to rows
      do j = 1 to cols
        src = ((j - 1 + k) // cols) + 1
        out = out word(rav, (i-1)*cols + src)
      end
    end
    return MkVal(shp, strip(out))
  end
  return RunErr('rotate: rank > 2 not supported')

/* x CAT y : vector concatenation, or row-stacking for equal-width matrices */
PrimCat: procedure
  parse arg x, y
  rx = Rank(x); ry = Rank(y)
  if rx <= 1 & ry <= 1 then do
    out = GetRavel(x) GetRavel(y)
    return MkVal(words(out), strip(out))
  end
  if rx = 2 & ry = 2 then do
    sx = GetShape(x); sy = GetShape(y)
    if word(sx,2) <> word(sy,2) then return RunErr('cat: matrices must have same column count')
    newrows = word(sx,1) + word(sy,1)
    out = GetRavel(x) GetRavel(y)
    return MkVal(newrows word(sx,2), strip(out))
  end
  return RunErr('cat: mismatched or unsupported ranks for concatenation')

/* bits COMPRESS x : keep vector elements (or matrix rows) where bit=1 */
PrimCompress: procedure
  parse arg bits, x
  bshp = GetShape(bits); brav = GetRavel(bits)
  r = Rank(x)
  shp = GetShape(x); rav = GetRavel(x)
  if r <= 1 then do
    if words(brav) <> words(rav) then return RunErr('compress: length mismatch')
    out = ''; cnt = 0
    do i = 1 to words(rav)
      if word(brav, i) = 1 then do
        out = out word(rav, i); cnt = cnt + 1
      end
    end
    return MkVal(cnt, strip(out))
  end
  if r = 2 then do
    rows = word(shp,1); cols = word(shp,2)
    if words(brav) <> rows then return RunErr('compress: bit-vector length must equal row count')
    out = ''; cnt = 0
    do i = 1 to rows
      if word(brav, i) = 1 then do
        do j = 1 to cols
          out = out word(rav, (i-1)*cols + j)
        end
        cnt = cnt + 1
      end
    end
    return MkVal(cnt cols, strip(out))
  end
  return RunErr('compress: rank > 2 not supported')

/* x INDEX i : vector element, matrix row, or matrix element via 2-vector */
PrimIndex: procedure
  parse arg x, idx
  r = Rank(x)
  shp = GetShape(x); rav = GetRavel(x)
  io = value('APLIO', , 'ENVIRONMENT')
  if io = '' then io = 1
  if r <= 1 then do
    if Rank(idx) = 0 then do
      i = GetRavel(idx) - io + 1
      if i < 1 | i > words(rav) then return RunErr('index: subscript out of range')
      return MkVal('', word(rav, i))
    end
    /* gather: vector of indices */
    out = ''
    do k = 1 to words(GetRavel(idx))
      i = word(GetRavel(idx), k) - io + 1
      if i < 1 | i > words(rav) then return RunErr('index: subscript out of range')
      out = out word(rav, i)
    end
    return MkVal(words(out), strip(out))
  end
  if r = 2 then do
    rows = word(shp,1); cols = word(shp,2)
    if Rank(idx) = 0 then do          /* select a whole row */
      i = GetRavel(idx) - io + 1
      if i < 1 | i > rows then return RunErr('index: row subscript out of range')
      out = ''
      do j = 1 to cols
        out = out word(rav, (i-1)*cols + j)
      end
      return MkVal(cols, strip(out))
    end
    if words(GetRavel(idx)) = 2 then do   /* (row col) -> single element */
      i = word(GetRavel(idx),1) - io + 1; j = word(GetRavel(idx),2) - io + 1
      if i < 1 | i > rows | j < 1 | j > cols then return RunErr('index: subscript out of range')
      return MkVal('', word(rav, (i-1)*cols + j))
    end
    return RunErr('index: expected scalar row index or 2-element (row col) index')
  end
  return RunErr('index: rank > 2 not supported')

/* standard matrix multiply / dot product (inner product +.*) */
PrimMatmul: procedure
  parse arg x, y
  rx = Rank(x); ry = Rank(y)
  if rx = 1 & ry = 1 then do
    rax = GetRavel(x); ray = GetRavel(y)
    if words(rax) <> words(ray) then return RunErr('matmul: length mismatch')
    s = 0
    do i = 1 to words(rax)
      s = s + word(rax,i) * word(ray,i)
    end
    return MkVal('', s)
  end
  if rx = 2 & ry = 2 then do
    sx = GetShape(x); sy = GetShape(y)
    ar = word(sx,1); ac = word(sx,2)
    br = word(sy,1); bc = word(sy,2)
    if ac <> br then return RunErr('matmul: inner dimensions must match')
    rax = GetRavel(x); ray = GetRavel(y)
    out = ''
    do i = 1 to ar
      do j = 1 to bc
        s = 0
        do k = 1 to ac
          s = s + word(rax,(i-1)*ac+k) * word(ray,(k-1)*bc+j)
        end
        out = out s
      end
    end
    return MkVal(ar bc, strip(out))
  end
  return RunErr('matmul: unsupported ranks (need vector.vector or matrix.matrix)')

/* ======================================================================
   []EA -- Execute Alternate, this dialect's rough equivalent of
   try/catch. Both operands are TEXT values holding a single APL
   statement's source; the primary is tokenized and evaluated in the
   CURRENT scope (so it can read/write a calling function's locals,
   just like real APL's ⎕EA). If it succeeds, its result is the result
   of the whole []EA expression. If it errors, the error is caught
   (cleared) and the alternate is executed and returned instead; if the
   alternate ALSO errors, that second error propagates normally.
   ====================================================================== */
PrimExecuteAlternate: procedure expose TOK. TOKN POS VARS. VARSET. VARLIST FUNC. USERFUNC. FNHDR. FNBODY. FNLINECOUNT.
  parse arg x, y
  if \IsText(x) | \IsText(y) then return RunErr('[]EA requires two text (executable-expression) operands')
  call value 'APLERR', '', 'ENVIRONMENT'
  call Tokenize GetRavel(x)
  if TOKN = 0 then return RunErr('[]EA: empty primary expression')
  prevSuppress = value('APLSUPPRESSERR', , 'ENVIRONMENT')
  call value 'APLSUPPRESSERR', '1', 'ENVIRONMENT'   /* catch silently, like try/catch */
  /* NOTE: this local is deliberately NOT named "result" -- REXX's CALL
     statement (used just below) auto-sets a variable literally named
     RESULT to whatever the called routine returned, which would
     silently clobber a same-named local the instant execution reaches
     that line. */
  primaryval = ParseStmt()
  call value 'APLSUPPRESSERR', prevSuppress, 'ENVIRONMENT'
  if value('APLERR', , 'ENVIRONMENT') = '' then return primaryval
  /* primary failed: it was caught silently -- now try the alternate,
     whose own errors (if any) DO display normally */
  call value 'APLERR', '', 'ENVIRONMENT'
  call Tokenize GetRavel(y)
  if TOKN = 0 then return RunErr('[]EA: empty alternate expression')
  return ParseStmt()   /* if this also errors, APLERR stays set and propagates */

/* ======================================================================
   OPERATORS: reduce / scan / each / outer
   ====================================================================== */

/* reduce along the last axis; vector -> scalar, matrix -> column vector
   of per-row reductions.  Fold direction is right-to-left, as in APL. */
ApplyReduce: procedure
  parse arg fn, x
  if IsText(x) then return RunErr('DOMAIN ERROR: reduce does not accept a text value')
  r = Rank(x)
  shp = GetShape(x); rav = GetRavel(x)
  if r <= 1 then do
    n = words(rav)
    if n = 0 then return RunErr('reduce: empty vector')
    acc = word(rav, n)
    do i = n-1 to 1 by -1
      acc = ScalarBin(fn, word(rav, i), acc)
    end
    return MkVal('', acc)
  end
  if r = 2 then do
    rows = word(shp,1); cols = word(shp,2)
    out = ''
    do i = 1 to rows
      acc = word(rav, (i-1)*cols + cols)
      do j = cols-1 to 1 by -1
        acc = ScalarBin(fn, word(rav, (i-1)*cols + j), acc)
      end
      out = out acc
    end
    return MkVal(rows, strip(out))
  end
  return RunErr('reduce: rank > 2 not supported')

/* cumulative fold, left-to-right; vector only */
ApplyScan: procedure
  parse arg fn, x
  if IsText(x) then return RunErr('DOMAIN ERROR: scan does not accept a text value')
  if Rank(x) > 1 then return RunErr('scan: vector argument required')
  rav = GetRavel(x)
  n = words(rav)
  out = ''
  acc = word(rav, 1)
  out = acc
  do i = 2 to n
    acc = ScalarBin(fn, acc, word(rav, i))
    out = out acc
  end
  return MkVal(n, strip(out))

/* apply a monadic scalar function to every element, shape preserved */
ApplyEach: procedure
  parse arg fn, x
  if IsText(x) then return RunErr('DOMAIN ERROR: each does not accept a text value')
  return ElementwiseMonadic(fn, x)

/* outer product table: shape = (na, nb) */
ApplyOuter: procedure
  parse arg fn, a, b
  if IsText(a) | IsText(b) then return RunErr('DOMAIN ERROR: outer does not accept a text value')
  if Rank(a) > 1 | Rank(b) > 1 then return RunErr('outer: vector arguments required')
  ra = GetRavel(a); rb = GetRavel(b)
  na = words(ra); nb = words(rb)
  out = ''
  do i = 1 to na
    do j = 1 to nb
      out = out ScalarBin(fn, word(ra,i), word(rb,j))
    end
  end
  return MkVal(na nb, strip(out))

/* ======================================================================
   PRINTING
   ====================================================================== */
PrintVal: procedure
  parse arg v
  if IsText(v) then do
    say GetRavel(v)
    return
  end
  r = Rank(v)
  shp = GetShape(v); rav = GetRavel(v)
  if r = 0 then do
    say Fmt(rav)
    return
  end
  if r = 1 then do
    line = ''
    do i = 1 to words(rav)
      line = line Fmt(word(rav,i))
    end
    say strip(line)
    return
  end
  if r = 2 then do
    rows = word(shp,1); cols = word(shp,2)
    /* compute column width for neat alignment */
    w = 1
    do i = 1 to words(rav)
      L = length(Fmt(word(rav,i)))
      if L > w then w = L
    end
    do i = 1 to rows
      line = ''
      do j = 1 to cols
        line = line right(Fmt(word(rav,(i-1)*cols+j)), w)
      end
      say strip(line, 'L')
    end
    return
  end
  /* rank > 2: fall back to shape + flat ravel */
  say 'shape' shp ':' rav
  return

/* ======================================================================
   SYSTEM COMMANDS  ( )VARS  )FNS  )LIB  )CLEAR  )SAVE  )LOAD  )HELP  )OFF )
   A line whose first non-blank character is ')' is a session command,
   not an APL statement -- handled here instead of by the normal
   tokenizer/parser.  Returns 'QUIT' if the caller's read loop should
   end the session, '' otherwise.
   ====================================================================== */
HandleSysCmd: procedure expose VARS. VARSET. VARLIST FUNC. FUNCLIST OPLIST ALIASLIST,
                                USERFUNC. FNHDR. FNBODY. FNLINECOUNT. FNLIST
  parse arg ln
  parse var ln cmd rest
  ucmd = translate(cmd)
  select
    when ucmd = ')VARS'  then call SysVars
    when ucmd = ')FNS'   then call SysFns
    when ucmd = ')PRIMS' then call SysPrims
    when ucmd = ')LIB'   then call SysLib
    when ucmd = ')WSID'  then call SysWsid strip(rest)
    when ucmd = ')CLEAR' then call SysClear
    when ucmd = ')SAVE'  then call SysSave strip(rest)
    when ucmd = ')LOAD'  then call SysLoad strip(rest)
    when ucmd = ')ERASE' then call SysErase strip(rest)
    when ucmd = ')HELP' | ucmd = ')?' then call SysHelp
    when ucmd = ')QUIT' | ucmd = ')EXIT' | ucmd = ')OFF' then return 'QUIT'
    otherwise say 'apl: unknown system command:' cmd '(try )HELP)'
  end
  return ''

/* )VARS -- list the names of currently-defined variables, sorted,
   space-separated -- exactly what real APL's )VARS shows (no shapes or
   values; use the variable's own name as an expression to see those) */
SysVars: procedure expose VARS. VARSET. VARLIST
  names = ''
  do i = 1 to words(VARLIST)
    nm = word(VARLIST, i)
    if VARSET.nm then names = names nm
  end
  names = strip(names)
  if names = '' then do
    say '(no variables defined)'
    return
  end
  /* simple ascending sort (insertion sort; variable counts are small) */
  sorted = ''
  do i = 1 to words(names)
    w = word(names, i)
    do j = 1 to words(sorted)
      if w < word(sorted, j) then leave
    end
    sorted = subword(sorted, 1, j-1) w subword(sorted, j)
  end
  say strip(sorted)
  return

/* )FNS -- list user-defined function names. This dialect has no
   user-defined-function facility (every function is a fixed primitive,
   listed instead by )PRIMS), so this always reports none defined --
   matching what real APL's )FNS would show in a workspace where you'd
   never typed a function definition. */
/* )FNS -- list user-defined function names (with their headers). Real
   functions now exist here (defined via nabla/edit), so this lists
   them the same way )VARS lists variable names. */
SysFns: procedure expose USERFUNC. FNHDR. FNLIST
  names = ''
  do i = 1 to words(FNLIST)
    nm = word(FNLIST, i)
    if USERFUNC.nm = 1 then names = names nm
  end
  names = strip(names)
  if names = '' then do
    say '(no user-defined functions -- use nabla/edit to define one; see )PRIMS for built-ins)'
    return
  end
  do i = 1 to words(names)
    nm = word(names, i)
    hdr = FNHDR.nm
    parse var hdr arity '|' resultvar '|' paramlist '|' locallist
    say FormatHeader(nm, arity, resultvar, paramlist)
  end
  return

/* )PRIMS -- list the primitive functions, operators, and quad variables
   built into this dialect */
SysPrims: procedure expose FUNCLIST OPLIST ALIASLIST
  say 'primitive functions (monadic and/or dyadic):'
  say '  ' FUNCLIST
  say 'higher-order operators (prefix form -- e.g. "reduce plus v"):'
  say '  ' OPLIST
  say 'word aliases usable as a reduce/scan/each/outer function name:'
  say '  ' ALIASLIST
  say 'quad system variables/functions:'
  say '  []IO  (settable, 0 or 1)   []PP  (settable, decimal places)   []TS  (read-only)'
  say '  []EA  (dyadic: primary []EA alternate -- Execute Alternate, i.e. try/catch)'
  say 'branching (inside a nabla-defined function only):'
  say '  -> n        branch to line n          -> label     branch to a label'
  say '  ->          exit the function         -> cond compress label   conditional branch'
  return

/* )LIB -- list .apl script files in the current directory */
/* )LIB -- list saved workspaces (i.e. .apl files) in the current
   directory, shown as plain workspace names (no .apl extension),
   matching real APL's )LIB. The active workspace (see )WSID) is marked
   with '*' if it appears among them. */
SysLib: procedure
  address system 'ls *.apl 2>/dev/null' with output stem lib.
  if lib.0 = 0 then do
    say '(no saved workspaces in the current directory)'
    return
  end
  active = value('APLWSID', , 'ENVIRONMENT')
  names = ''
  do i = 1 to lib.0
    names = names WsBaseName(lib.i)
  end
  names = strip(names)
  /* simple ascending sort */
  sorted = ''
  do i = 1 to words(names)
    w = word(names, i)
    do j = 1 to words(sorted)
      if w < word(sorted, j) then leave
    end
    sorted = subword(sorted, 1, j-1) w subword(sorted, j)
  end
  say 'workspaces available in this directory:'
  do i = 1 to words(sorted)
    nm = word(sorted, i)
    if nm = active then say '  'nm '  (active)'
    else say '  'nm
  end
  return

/* )CLEAR -- wipe all variables and reset quad system variables to
   their defaults, i.e. a fresh workspace */
SysClear: procedure expose VARS. VARSET. VARLIST USERFUNC. FNHDR. FNBODY. FNLINECOUNT. FNLIST
  drop VARS. VARSET. USERFUNC. FNHDR. FNBODY. FNLINECOUNT.
  VARSET. = 0
  VARLIST = ''
  USERFUNC. = 0
  FNLIST = ''
  call value 'APLIO', 1, 'ENVIRONMENT'
  call value 'APLPP', 6, 'ENVIRONMENT'
  call value 'APLCALLDEPTH', 0, 'ENVIRONMENT'
  call value 'APLWSID', 'CLEAR WS', 'ENVIRONMENT'
  say 'workspace cleared.'
  return

/* removes every occurrence of word w from a space-separated list */
RemoveWord: procedure
  parse arg list, w
  out = ''
  do i = 1 to words(list)
    if word(list, i) <> w then out = out word(list, i)
  end
  return strip(out)

/* )ERASE name [name2 ...] -- remove one or more variables and/or
   user-defined functions from the workspace. A name can be a variable,
   a function, or (unusually) somehow both are not possible in this
   dialect since the two namespaces are shared -- but )ERASE checks and
   removes whichever it actually is. Names that don't exist are reported,
   not silently ignored, since a typo here is easy to make. */
SysErase: procedure expose VARS. VARSET. VARLIST USERFUNC. FNHDR. FNBODY. FNLINECOUNT. FNLIST
  parse arg namelist
  namelist = strip(namelist)
  if namelist = '' then do
    say 'usage: )ERASE name [name2 ...]'
    return
  end
  do i = 1 to words(namelist)
    nm = word(namelist, i)
    erased = 0
    if VARSET.nm = 1 then do
      drop VARS.nm
      VARSET.nm = 0
      VARLIST = RemoveWord(VARLIST, nm)
      erased = 1
    end
    if USERFUNC.nm = 1 then do
      do j = 1 to FNLINECOUNT.nm
        drop FNBODY.nm.j
      end
      drop FNHDR.nm
      drop FNLINECOUNT.nm
      USERFUNC.nm = 0
      FNLIST = RemoveWord(FNLIST, nm)
      erased = 1
    end
    if erased then say 'erased:' nm
    else say 'apl: not found (no such variable or function):' nm
  end
  return

/* )WSID [name] -- report (bare) or set (with a name) the active
   workspace identifier. Setting it does NOT save anything to disk --
   exactly matching real APL, where )WSID only renames the in-memory
   workspace; use )SAVE to actually write it out. */
SysWsid: procedure
  parse arg newname
  newname = strip(newname)
  if newname = '' then do
    say value('APLWSID', , 'ENVIRONMENT')
    return
  end
  call value 'APLWSID', WsBaseName(newname), 'ENVIRONMENT'
  say 'workspace id set to:' value('APLWSID', , 'ENVIRONMENT')
  return

/* )SAVE <file> -- write all current variables AND user-defined functions
   out as a re-loadable .apl script: variables as assignments (rank<=1 as
   a bare literal, rank 2 via dyadic rho), functions as nabla/edit blocks.
   Does NOT persist []IO/[]PP settings. */
/* )SAVE [name] -- write all current variables AND user-defined functions
   out as a re-loadable .apl script: variables as assignments (rank<=1 as
   a bare literal, rank 2 via dyadic rho), functions as nabla/edit blocks.
   With no name, saves under the active workspace id (see )WSID); a bare
   name (no .apl) is accepted and the extension is added automatically.
   On success, the workspace id becomes this save's name.
   Does NOT persist []IO/[]PP settings. */
SysSave: procedure expose VARS. VARSET. VARLIST USERFUNC. FNHDR. FNBODY. FNLINECOUNT. FNLIST
  parse arg wsname
  wsname = strip(wsname)
  if wsname = '' then wsname = value('APLWSID', , 'ENVIRONMENT')
  if wsname = '' | wsname = 'CLEAR WS' then do
    say 'usage: )SAVE name   (or set one first with )WSID name)'
    return
  end
  fn = WsFileName(wsname)
  nv = 0; nf = 0
  if strip(VARLIST) = '' & strip(FNLIST) = '' then do
    say '(nothing to save)'
    return
  end
  call stream fn, 'c', 'open write replace'
  call lineout fn, '# saved by )SAVE'
  do i = 1 to words(VARLIST)
    nm = word(VARLIST, i)
    if \VARSET.nm then iterate
    v = VARS.nm
    shp = GetShape(v); rav = GetRavel(v)
    if shp = '' then
      call lineout fn, nm '<-' rav
    else if words(shp) = 1 then
      call lineout fn, nm '<- ('rav')'
    else
      call lineout fn, nm '<- ('shp') rho ('rav')'
    nv = nv + 1
  end
  do i = 1 to words(FNLIST)
    nm = word(FNLIST, i)
    if USERFUNC.nm \= 1 then iterate
    hdr = FNHDR.nm
    parse var hdr arity '|' resultvar '|' paramlist '|' locallist
    hdrtext = FormatHeader(nm, arity, resultvar, paramlist)
    if locallist <> '' then do
      ll = ''
      do k = 1 to words(locallist)
        ll = ll';'word(locallist, k)
      end
      hdrtext = hdrtext || ll
    end
    call lineout fn, 'nabla' hdrtext
    do j = 1 to FNLINECOUNT.nm
      call lineout fn, FNBODY.nm.j
    end
    call lineout fn, 'nabla'
    nf = nf + 1
  end
  call stream fn, 'c', 'close'
  call value 'APLWSID', WsBaseName(fn), 'ENVIRONMENT'
  say 'saved' nv 'variable(s) and' nf 'function(s) to' fn
  return

/* )LOAD <file> -- run a script file's statements into the CURRENT
   session (variables persist afterward); a bad statement in the file
   prints a message and continues with the file's next line, the same
   way the REPL itself recovers from errors. */
SysLoad: procedure expose VARS. VARSET. VARLIST FUNC. USERFUNC. FNHDR. FNBODY. FNLINECOUNT. FNLIST
  parse arg wsname
  wsname = strip(wsname)
  if wsname = '' then do
    say 'usage: )LOAD name'
    return
  end
  fn = WsResolveLoad(wsname)
  if fn = '' then do
    say 'apl: cannot open' wsname '(tried' WsFileName(wsname) 'and' wsname')'
    return
  end
  call value 'APLERR', '', 'ENVIRONMENT'
  n = 0
  do while lines(fn) > 0
    ln = linein(fn)
    lnstrip = strip(ln)
    ucfirst = translate(word(lnstrip, 1))
    if ucfirst = 'NABLA' | ucfirst = 'EDIT' then do
      call EnterDefineMode strip(subword(lnstrip, 2)), fn
      iterate
    end
    if left(lnstrip, 1) = ')' then iterate  /* nested sys-commands ignored */
    call Tokenize ln
    if TOKN = 0 then iterate
    call value 'APLERR', '', 'ENVIRONMENT'
    call value 'APLBRANCH', '', 'ENVIRONMENT'
    result = ParseStmt()
    if value('APLERR', , 'ENVIRONMENT') = '' then do
      if value('APLBRANCH', , 'ENVIRONMENT') <> '' then do
        say 'apl: -> (branch) can only be used inside a function body'
        call value 'APLBRANCH', '', 'ENVIRONMENT'
      end
      else if result <> '' then call PrintVal result
    end
    n = n + 1
  end
  call stream fn, 'c', 'close'
  call value 'APLWSID', WsBaseName(fn), 'ENVIRONMENT'
  say 'loaded' fn
  return

SysHelp: procedure
  say 'system commands:'
  say '  )VARS         list defined variable names'
  say '  )FNS          list user-defined function names (define one with nabla/edit)'
  say '  )PRIMS        list built-in primitive functions/operators/quad-vars'
  say '  )LIB          list saved workspaces in the current directory'
  say '  )WSID [name]  show, or set, the active workspace id'
  say '  )CLEAR        wipe all variables and functions (fresh workspace)'
  say '  )SAVE [name]  save the workspace (defaults to the active )WSID)'
  say '  )LOAD name    load a saved workspace into the current session'
  say '  )ERASE name [name2 ...]   delete variable(s)/function(s) by name'
  say '  )HELP  or )?  show this message'
  say '  )QUIT )EXIT )OFF   end the session'
  say ''
  say 'nabla Z<-NAME X   (or "edit ...")  define a function; see regina_apl_doc.md'
  return

/* ======================================================================
   MAIN DRIVER: read & execute a source file line by line
   ====================================================================== */
RunFile: procedure expose FUNC. VARS. VARSET. VARLIST FUNCLIST OPLIST ALIASLIST,
                           USERFUNC. FNHDR. FNBODY. FNLINECOUNT. FNLIST
  parse arg fn
  call value 'APLMODE', 'FILE', 'ENVIRONMENT'
  call value 'APLERR', '', 'ENVIRONMENT'
  lineno = 0
  do while lines(fn) > 0
    ln = linein(fn)
    lineno = lineno + 1
    lnstrip = strip(ln)
    ucfirst = translate(word(lnstrip, 1))
    if ucfirst = 'NABLA' | ucfirst = 'EDIT' then do
      call EnterDefineMode strip(subword(lnstrip, 2)), fn
      iterate
    end
    if left(lnstrip, 1) = ')' then do
      rc = HandleSysCmd(lnstrip)
      if rc = 'QUIT' then leave
      iterate
    end
    call Tokenize ln
    if TOKN = 0 then iterate
    result = ParseStmt()
    if value('APLERR', , 'ENVIRONMENT') <> '' then do
      call stream fn, 'c', 'close'
      exit 1
    end
    if value('APLBRANCH', , 'ENVIRONMENT') <> '' then do
      say 'apl: -> (branch) can only be used inside a function body'
      call value 'APLBRANCH', '', 'ENVIRONMENT'
    end
    else if result <> '' then call PrintVal result
  end
  call stream fn, 'c', 'close'
  return

/* ======================================================================
   INTERACTIVE REPL
   Started automatically when apl.rex is run with no source-file argument.
   Reads one statement per line from standard input, evaluates it the
   same way a script line would, and prints the result -- the classic
   APL "immediate execution" experience.  A syntax or runtime error
   prints a message and returns to the prompt rather than exiting.
   Type )quit (or )exit, )off, or end-of-input / Ctrl-D) to leave, or
   )help for a list of session commands. Type "nabla <header>" (or
   "edit <header>") to define a function -- see regina_apl_doc.md.
   ====================================================================== */
RunRepl: procedure expose FUNC. VARS. VARSET. VARLIST FUNCLIST OPLIST ALIASLIST,
                           USERFUNC. FNHDR. FNBODY. FNLINECOUNT. FNLIST
  call value 'APLMODE', 'REPL', 'ENVIRONMENT'
  call value 'APLERR', '', 'ENVIRONMENT'
  say 'APL-in-REXX  --  type )help for commands, )quit to exit'
ReplLoopTop:
  call charout , 'apl> '
  if lines() <= 0 then do
    say
    say 'goodbye.'
    return
  end
  ln = linein()
  lnstrip = strip(ln)
  ucfirst = translate(word(lnstrip, 1))
  if ucfirst = 'NABLA' | ucfirst = 'EDIT' then do
    call EnterDefineMode strip(subword(lnstrip, 2)), ''
    signal ReplLoopTop
  end
  if left(lnstrip, 1) = ')' then do
    rc = HandleSysCmd(lnstrip)
    if rc = 'QUIT' then do
      say 'goodbye.'
      return
    end
    signal ReplLoopTop
  end
  call Tokenize ln
  if TOKN = 0 then signal ReplLoopTop
  call value 'APLERR', '', 'ENVIRONMENT'
  call value 'APLBRANCH', '', 'ENVIRONMENT'
  result = ParseStmt()
  if value('APLERR', , 'ENVIRONMENT') = '' then do
    if value('APLBRANCH', , 'ENVIRONMENT') <> '' then do
      say 'apl: -> (branch) can only be used inside a function body'
      call value 'APLBRANCH', '', 'ENVIRONMENT'
    end
    else if result <> '' then call PrintVal result
  end
  signal ReplLoopTop
