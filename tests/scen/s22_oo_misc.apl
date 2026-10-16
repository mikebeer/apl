# interplay: classic functions, locals, recursion, errors inside methods
nabla R<-Fact N
:If N < 2
  R<-1
  :Return
:EndIf
R<-N times Fact N minus 1
nabla
:Class Calc
  :Field Public memo <- 0
  :Access Public
  nabla R<-Fact N
  :If N < 2
    R<-1
    :Return
  :EndIf
  R<-N times Fact N minus 1
  nabla
  :Access Public
  nabla R<-Sum V;t;i
  t<-0
  :For i :In V
    t<-t plus i
  :EndFor
  R<-t
  nabla
  :Access Public
  nabla R<-Boom X
  R<-X plus 'a'
  nabla
  :Access Public
  nabla R<-UseRoot X
  R<-RootFn X
  nabla
:EndClass
nabla R<-RootFn X
R<-X plus 1000
nabla
c <- []NEW Calc
# the method Fact calls itself (its own class scope), the root Fact is separate
c.Fact 5
Fact 5
c.Sum 1 2 3 4
# t and i were local to the method call
('t' []EA '''no t''')
# a root function can be called from inside a method
c.UseRoot 1
# an error inside a method leaves the interpreter in the root scope
('c.Boom 1' []EA '[]DM')
RootFn 5
Fact 4
# workspace-level variables are not seen as fields
memo <- 'root memo'
c.memo
memo
# an instance can be given to an ordinary function
nabla R<-GetM O
R<-O.memo
nabla
c.memo <- 77
GetM c
# classic scoping: a local hides a global of the same name for callees
g <- 'global'
nabla R<-Inner
R<-g
nabla
nabla R<-Outer;g
g<-'local'
R<-Inner
nabla
Outer
Inner
