# object-related system functions and commands
:Namespace Lib
  :Class Stack
    :Field Public items <- zilch
    :Access Public
    nabla Push X
    items <- items cat X
    nabla
    :Access Public
    nabla R<-Size
    R<-tally items
    nabla
    :Access Public
    nabla R<-Me
    R<-[]THIS
    nabla
  :EndClass
:EndNamespace
)CLASSES
)NSS
s <- []NEW Lib.Stack
s.Push 3
s.Push 4
s.Size
s.items
s.items[1] <- 99
s.items
m <- s.Me
m match s
# []NC classes: 2 variable 3 function 9.1 namespace 9.2 instance 9.4 class
[]NC 'Lib'
[]NC 's'
[]NC 'Lib.Stack'
[]NC 's.items'
[]NC 's.Push'
[]NC 'Nothing'
# []CR and []NR show a method's source
[]CR 'Lib.Stack.Size'
[]NR 'Lib.Stack.Size'
# []FIX defines a whole class from its source lines
q <- []FIX ':Class Q' ':Field Public z <- 7' ':EndClass'
q
r <- []NEW Q
r.z
# []DF changes how an instance is displayed
r []DF 'my Q'
r
[]DF r
# the system names
[]ROOT
[]THIS
# class redefinition, with and without live instances
x <- []FIX ':Class Q' ':Field Public z <- 8' ':EndClass'
r <- 0
x <- []FIX ':Class Q' ':Field Public z <- 8' ':EndClass'
([]NEW Q).z
# expunging
[]EX 'Q'
)CLASSES
('[]EX ''Lib''' []EA '[]DM')
s <- 0
m <- 0
[]EX 'Lib'
)NSS
