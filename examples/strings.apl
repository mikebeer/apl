# strings.apl - text is an array of characters
t <- 'The quick brown fox'
rho t
reverse t
t[1 2 3]
vowels <- t member 'aeiou'
vowels
vowels compress t
(not vowels) compress t
'abc' = 'abd'
'Hello, ' cat 'world'
m <- (2 5) rho 'applepears'
m
transpose m
gradeup 'banana'
unique 'mississippi'
words <- 'one' 'two' 'three'
each reverse words
' ' cat 'x'
