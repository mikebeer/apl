# an object can act as an auxiliary processor, by object or by number
:Class Ticker
  :Field Private n <- 0
  :Field Private ev <- zilch
  :Access Public
  nabla Offer V
  ev <- ev cat 1
  nabla
  :Access Public
  nabla R<-Get V
  n <- n plus 1
  R<-n
  nabla
  :Access Public
  nabla R<-V Set X
  R<-X times 2
  nabla
  :Access Public
  nabla Retract V
  ev <- ev cat 0
  nabla
  :Access Public
  nabla R<-Log
  R<-ev
  nabla
:EndClass
t <- []NEW Ticker
# object form: the object is the left argument of []SVO
ok <- t []SVO 'X'
X
X
X <- 21
X
# number form: register the object as processor 7, then use the number
7 []PROC t
7 []SVO 'Y'
Y
)PROCS
[]SVR 'X'
t.Log
# the classic built-in processors are unchanged
2 []SVO 'U'
U <- 'hello'
U
3 []SVO 'C'
C
C
# processors and the objects behind them are saved
)SAVE s21ws
)CLEAR
)PROCS
)LOAD s21ws
)PROCS
Y
t.Log
