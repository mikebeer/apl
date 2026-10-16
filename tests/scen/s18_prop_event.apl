# properties (get/set) and events
:Class Temp
  :Field Private c <- 0
  :Event Changed
  :Property Public F
    nabla R<-get
    R<-32 plus 1.8 times c
    nabla
    nabla set V;ok
    c <- (V minus 32) divide 1.8
    ok <- []NQ []THIS 'Changed' c
    nabla
  :EndProperty
  :Property Public C
    nabla R<-get
    R<-c
    nabla
  :EndProperty
  :Constructor
  nabla Init X
  c <- X
  nabla
:EndClass
t <- []NEW Temp 100
t.F
t.C
t.F <- 32
t.C
# C has no set function
('execute ''t.C <- 5''' []EA '[]DM')
# a handler is any function reachable by name; it is called with the data
:Class Watcher
  :Field Public seen <- 0
  :Field Public last <- 0
  :Access Public
  nabla OnChange X
  seen <- seen plus 1
  last <- X
  nabla
:EndClass
w <- []NEW Watcher
x <- t []ON 'Changed' 'w.OnChange'
t.F <- 50
t.F <- 212
w.seen
w.last
# a plain function can listen too
nabla Log X
'changed to ' cat format X
nabla
x <- t []ON 'Changed' 'Log'
t.F <- 32
w.seen
# events that were never declared are an error
('t []ON ''Nope'' ''Log''' []EA '[]DM')
('[]NQ t ''Nope''' []EA '[]DM')
