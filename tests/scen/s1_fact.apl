nabla Z<-FACT N
Z<-1
LOOP:
-> (N<=1) compress DONE
Z<-Z*N
N<-N - 1
-> LOOP
DONE:
nabla
FACT 5
FACT 10
FACT 1
nabla Z<-RFACT N
-> (N<=1) compress BASE
Z<-N * RFACT N - 1
->
BASE:
Z<-1
nabla
RFACT 6
nabla COUNTDOWN N
LOOP: []<-N
N<-N - 1
-> (N>0) compress LOOP
nabla
COUNTDOWN 3
