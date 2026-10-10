# control.apl - structured control words inside a function
nabla Z<-CLASSIFY N
:If N<0
  Z<-'negative'
:ElseIf N=0
  Z<-'zero'
:Else
  Z<-'positive'
:EndIf
nabla

nabla Z<-SUMTO N;I
Z<-0
I<-1
:While I<=N
  Z<-Z+I
  I<-I+1
:EndWhile
nabla

nabla Z<-SQUARES V;X
Z<-zilch
:For X :In V
  :If X=3
    :Continue
  :EndIf
  :If X>6
    :Leave
  :EndIf
  Z<-Z cat X*X
:EndFor
nabla

nabla Z<-NAME D
:Select D
:Case 1
  Z<-'one'
:CaseList 2 3
  Z<-'few'
:Else
  Z<-'many'
:EndSelect
nabla

CLASSIFY neg 5
CLASSIFY 0
CLASSIFY 7
SUMTO 100
SQUARES iota 8
NAME 1
NAME 3
NAME 9
