# classes: fields, methods, constructors, destructors, shared members,
# access rules, class redefinition, references as values
:Class Account
  :Field Public balance <- 0
  :Field Private owner <- 'nobody'
  :Field Shared count <- 0
  :Field Public ReadOnly kind <- 'checking'
  :Constructor
  nabla Make X
  balance <- X
  count <- count plus 1
  nabla
  :Access Public
  nabla Deposit X
  balance <- balance plus X
  nabla
  :Access Public
  nabla R<-Balance
  R<-balance
  nabla
  :Access Public Shared
  nabla R<-Count
  R<-count
  nabla
  :Access Public
  nabla SetOwner N
  owner <- N
  nabla
  :Access Public
  nabla R<-Owner
  R<-owner
  nabla
  :Destructor
  nabla Bye
  count <- count minus 1
  nabla
:EndClass
a <- []NEW Account 100
a.Deposit 50
a.Balance
a.balance
a
b <- []NEW Account 7
Account.Count
b.SetOwner 'mike'
b.Owner
a.Owner
# private and read-only members are protected
('a.owner' []EA '[]DM')
('execute ''a.kind <- 1''' []EA '[]DM')
('a.kind' []EA '0')
a.balance <- 5
a.Balance
# a reference is a value: it can be stored, compared and nested
c <- a
c match a
c match b
objs <- a b c
tally objs
objs
a.Deposit 1
c.Balance
# an instance lives while anything refers to it; the destructor runs
# at the end of the statement that dropped the last reference
b <- 0
Account.Count
objs <- 0
Account.Count
a <- 0
Account.Count
c <- 0
Account.Count
[]INSTANCES Account
# class and instance information
)CLASSES
[]CLASS ([]NEW Account 1)
d <- []NEW Account 2
d []ISA Account
Account.[]NL 3
d.[]NL 2
d.[]NL 3
# []NEW with a name, and an error for a missing argument
('[]NEW ''Account''' []EA '[]DM')
('[]NEW Account 1 2' []EA '[]DM')
