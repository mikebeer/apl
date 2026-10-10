# stats.apl - mean, median and standard deviation
nabla Z<-MEAN V
Z<-(reduce plus V) divide tally V
nabla

nabla Z<-MEDIAN V;S;N
S<-V[gradeup V]
N<-tally S
Z<-(S[ceil N divide 2] plus S[floor (N plus 2) divide 2]) divide 2
nabla

nabla Z<-STDDEV V;D
D<-V minus MEAN V
Z<-(MEAN D times D) power 0.5
nabla

x <- 2 4 4 4 5 5 7 9
MEAN x
MEDIAN x
STDDEV x
y <- iota 10
MEAN y
MEDIAN y
STDDEV y
