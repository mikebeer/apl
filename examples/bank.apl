# bank.apl - functions, locals, branching and error trapping
balance <- 100

nabla DEPOSIT N
balance <- balance + N
nabla

nabla WITHDRAW N
-> (N<=balance) compress OK
'Insufficient funds'
->
OK: balance <- balance - N
nabla

nabla Z<-REPORT NAME
Z<-NAME cat ': ' cat format balance
nabla

REPORT 'Ann'
DEPOSIT 50
REPORT 'Ann'
WITHDRAW 30
REPORT 'Ann'
WITHDRAW 1000
REPORT 'Ann'
# an error inside []EA is caught and the alternative is evaluated
'balance divide 0' []EA '-1'
