# )SAVE and )LOAD keep classes, namespaces, objects, processors
:Interface IName
  :Method Name
:EndInterface
:Namespace Cfg
  lvl <- 3
  nabla R<-Deep
  R<-lvl times 2
  nabla
:EndNamespace
:Class Item :Implements IName
  :Field Public label <- 'x'
  :Field Public qty <- 0
  :Field Private secret <- 42
  :Field Shared made <- 0
  :Event Moved
  :Constructor
  nabla Make L
  label <- L
  made <- made plus 1
  nabla
  :Access Public
  nabla R<-Name
  R<-label
  nabla
  :Access Public
  nabla R<-Secret
  R<-secret
  nabla
  :Access Public Shared
  nabla R<-Made
  R<-made
  nabla
  :Property Public Double
    nabla R<-get
    R<-qty times 2
    nabla
  :EndProperty
  :Destructor
  nabla Bye
  made <- made minus 1
  nabla
:EndClass
:Class Store
  :Field Public items <- zilch
  :Field Public home <- 0
:EndClass
a <- []NEW Item 'apple'
b <- []NEW Item 'pear'
a.qty <- 5
s <- []NEW Store
s.items <- a b
s.home <- a
n <- []NS ''
n.p <- 11 12
list <- 1 2 3
w <- []NEW Item 'watched'
:Class Logger
  :Field Public count <- 0
  :Access Public
  nabla Hear X
  count <- count plus 1
  nabla
:EndClass
lg <- []NEW Logger
x <- a []ON 'Moved' 'lg.Hear'
[]NQ a 'Moved' 0
lg.count
Cfg.lvl <- 5
x <- a []DF 'the apple'
a
)SAVE s20ws
)CLEAR
)VARS
)CLASSES
)NSS
)LOAD s20ws
)CLASSES
)NSS
)VARS
a.Name
a.qty
a.Double
a.Secret
Item.Made
s.items
(s.items) match a b
s.home match a
(tally s.items)
n.p
list
Cfg.Deep
a
lg.count
[]NQ a 'Moved' 0
lg.count
a []ISA IName
Item.[]NL 3
# a loaded workspace behaves like the original: the destructor still works
b <- 0
s <- 0
w <- 0
x <- 0
a <- 0
Item.Made
