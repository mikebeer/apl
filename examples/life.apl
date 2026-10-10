# life.apl - Conway's Game of Life on a 6 by 6 torus
nabla Z<-NEIGHBOURS B;R
R<-B plus (1 rotate[1] B) plus -1 rotate[1] B
Z<-(R plus (1 rotate R) plus -1 rotate R) minus B
nabla

nabla Z<-STEP B;N
N<-NEIGHBOURS B
Z<-(N = 3) or (B and N = 2)
nabla

nabla SHOW B
' #'[B plus 1]
nabla

b <- (6 6) rho 0
b[1;2] <- 1
b[2;3] <- 1
b[3;1 2 3] <- 1
SHOW b
''
SHOW STEP b
''
SHOW STEP STEP b
''
SHOW STEP STEP STEP STEP b
