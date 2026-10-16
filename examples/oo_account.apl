# Objects: a base class, a derived class, a property, an event and a namespace
:Namespace Bank
  :Class Account
    :Field Public Shared count <- 0
    :Field Private bal <- 0
    :Event Overdrawn
    :Property Public Balance
      nabla R<-get
      R<-bal
      nabla
    :EndProperty
    :Constructor
    nabla Make X
    bal <- X
    count <- count plus 1
    nabla
    :Destructor
    nabla Bye
    count <- count minus 1
    nabla
    :Access Public
    nabla Deposit X
    bal <- bal plus X
    nabla
    :Access Public
    nabla Withdraw X;ok
    :If X > bal
      ok <- []NQ []THIS 'Overdrawn' X
    :Else
      bal <- bal minus X
    :EndIf
    nabla
    :Access Public Shared
    nabla R<-Count
    R<-count
    nabla
  :EndClass
  :Class Savings :Extends Account
    :Field Public rate <- 0.05
    :Constructor
    nabla Make2 X
    []BASE.Make X
    nabla
    :Access Public
    nabla AddInterest
    []THIS.Deposit rate times []THIS.Balance
    nabla
  :EndClass
:EndNamespace
:Class Alarm
  :Field Public hits <- 0
  :Access Public
  nabla Ring X
  hits <- hits plus 1
  nabla
:EndClass
a <- []NEW Bank.Account 100
s <- []NEW Bank.Savings 200
a.Deposit 50
a.Balance
s.AddInterest
s.Balance
Bank.Account.Count
s []ISA Bank.Account
w <- []NEW Alarm
x <- a []ON 'Overdrawn' 'w.Ring'
a.Withdraw 1000
w.hits
a.Balance
('execute ''a.Balance <- 1''' []EA '[]DM')
Bank []NL 9.4
