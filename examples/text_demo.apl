# text_demo.apl -- string literals, concatenation, and explicit display

'hi mike'
msg <- 'hi mike'
msg
'it''s a quoted quote'
'hi ' cat 'mike'

# explicit display with the bare quad, handy inside functions
[]<-'about to compute...'
[]<-5+3

# a function that prints a trace message, then returns a value
nabla Z<-NOISY X
'computing square...'
Z<-X*X
nabla

NOISY 6
