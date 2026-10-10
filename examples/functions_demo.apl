# functions_demo.apl -- defining and using functions with nabla/edit
#
# This script defines a few small functions, then calls them. It also
# shows a function calling another function, and locals.

nabla Z<-SQUARE X
Z<-X*X
nabla

SQUARE 5
SQUARE (iota 5)

nabla Z<-A ADD B
Z<-A+B
nabla

3 ADD 4
(iota 3) ADD (iota 3)

# a function that calls another function
nabla Z<-SUMSQ V
Z<-reduce plus (SQUARE V)
nabla

SUMSQ (iota 4)

# locals: S and N are scratch variables private to this function
nabla Z<-AVG V;S;N
S<-reduce plus V
N<-rho V
Z<-S/N
nabla

AVG (iota 10)

# a niladic function (no arguments)
nabla Z<-ANSWER
Z<-42
nabla

ANSWER
5 + ANSWER
