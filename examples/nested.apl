# nested.apl - arrays of arrays
y <- (1 2) (3 4 5) 'abc'
y
rho y
depth y
tally y
2 pick y
first y
each rho y
enclose 1 2 3
disclose (1 2) (3 4 5)
enlist (1 2) (3 (4 5))
(1 2) match (1 2)
3 member (1 2) (3 4)
words <- 'apple' 'fig' 'banana'
each tally words
words[2]
reduce plus each tally words
