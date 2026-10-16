# namespaces: qualified names, :Namespace blocks, )CS, []NS, :With, []NL, []EX
:Namespace Util
  k <- 10
  nabla R<-Twice X
  R<-2 times X
  nabla
  nabla R<-AddK X
  R<-X plus k
  nabla
  Square <- { omega times omega }
:EndNamespace
Util.Twice 21
Util.AddK 5
Util.Square 7
Util.k <- 100
Util.AddK 5
# an unqualified name inside a namespace function is found in the namespace
# first, then in the root
g <- 1000
nabla R<-ViaRoot X
R<-X plus g
nabla
)CS Util
ViaRoot 1
)CS
# nested namespaces and the system names
:Namespace A.B
  v <- 'deep'
:EndNamespace
A.B.v
)NSS
[]NL 9
Util.[]NL 2
Util.[]NL 3
[]NC 'Util.Twice'
[]NC 'Util.k'
[]NC 'Util'
# a reference to a namespace is a value
n <- []NS 'Other'
Other.v <- 1 2 3
n.v
n
n match Other
# anonymous namespaces
x <- []NS ''
x.p <- 5
x.p plus 1
x
:With x
  q <- 7
  p plus q
:EndWith
x.[]NL 2
# the current space
)CS Util
k
Twice 4
)VARS
)FNS
)CS
)VARS
# classic names are not disturbed
Twice <- 5
Twice
[]EX 'Util'
)NSS
('Util.k' []EA '0')
