# []FMT: a small report, with complex numbers and a selective assignment
names <- 4 6 rho 'apples' cat 'pears ' cat 'plums ' cat 'grapes'
qty <- 12 7 120 3
price <- 0.5 1.25 0.08 12
total <- qty times price
'A6,I5,F9.2,F10.2' []FMT (names) (qty) (price) (total)
'''Total: '',F10.2' []FMT reduce plus total
# the roots of x*x + 2x + 5 are complex
disc <- (2 * 2) minus 4 times 5
((neg 2) plus disc power 0.5) divide 2
((neg 2) minus disc power 0.5) divide 2
# selective assignment
qty
(2 take qty) <- 0
qty
