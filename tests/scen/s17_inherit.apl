# inheritance, overriding, []BASE, interfaces, []ISA
:Interface IShape
  :Method Area
  :Method Name
:EndInterface
:Class Shape :Implements IShape
  :Field Private name <- 'shape'
  :Access Public
  nabla R<-Name
  R<-name
  nabla
  :Access Public
  nabla R<-Area
  R<-0
  nabla
  :Access Public
  nabla R<-Describe
  R<-(Name) cat ' area ' cat format Area
  nabla
:EndClass
:Class Rect :Extends Shape
  :Field Public w <- 2
  :Field Public h <- 3
  :Override
  :Access Public
  nabla R<-Area
  R<-w times h
  nabla
  :Override
  :Access Public
  nabla R<-Name
  R<-'rect'
  nabla
:EndClass
r <- []NEW Rect
r.Describe
r.w <- 10
r.Area
s <- []NEW Shape
s.Describe
r []ISA Shape
r []ISA IShape
s []ISA Rect
r []ISA Rect
# constructors: a derived class calls the base constructor explicitly
:Class Base
  :Field Public a <- 1
  :Constructor
  nabla Init X
  a <- X
  nabla
  :Access Public
  nabla R<-Show
  R<-'base ' cat format a
  nabla
  :Destructor
  nabla Bye
  'base destroyed'
  nabla
:EndClass
:Class Derived :Extends Base
  :Field Public b <- 2
  :Constructor
  nabla Init2 X
  []BASE.Init X
  b <- X times 10
  nabla
  :Override
  :Access Public
  nabla R<-Show
  R<-('derived ' cat format b) cat ' / ' cat []BASE.Show
  nabla
  :Destructor
  nabla Bye2
  'derived destroyed'
  nabla
:EndClass
d <- []NEW Derived 5
d.Show
d.a
d <- 0
'after'
)CLASSES
# rules checked when a class is completed
:Interface IFull
  :Method Run
:EndInterface
:Class NoRun :Implements IFull
:EndClass
:Class Shadow :Extends Shape
  :Access Public
  nabla R<-Area
  R<-1
  nabla
:EndClass
:Class Lonely
  :Override
  :Access Public
  nabla R<-Ghost
  R<-1
  nabla
:EndClass
)CLASSES
