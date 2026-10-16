# []FMT: format phrases, qualifiers, blocks of columns
'I5' []FMT 42
'I5' []FMT 1 2 3
'F8.2' []FMT 3.14159
'F8.2' []FMT 3 2 rho 1.5 2.25 100 0.5 (neg 1) 7
'I3,F7.1' []FMT 3 2 rho 1 2.5 3 4.5 5 6.5
'2I4' []FMT 2 3 rho iota 6
'I3,2F6.1' []FMT 2 3 rho 1 2 3 4 5 6
'''Total: '',F8.2' []FMT 1234.5
'CF12.2' []FMT 1234567.891
'BI4' []FMT 0 5
'ZI5' []FMT 42
'LI5,''|''' []FMT 42
'E12.4' []FMT 12345.678
'I2' []FMT 100
'A5' []FMT 'hello'
'A8,I4' []FMT (2 4 rho 'ann bob ') (1 2)
x <- 'ann' 'bob'
'G''99/99/99''' []FMT 70586
'I4,X2,I4' []FMT 2 2 rho 1 2 3 4
'K2F6.1' []FMT 1.234
'I3,T10,I3' []FMT 2 2 rho 1 2 3 4
'M''CR'' F8.2' []FMT neg 5.5
'2(I2,''-'')' []FMT 2 2 rho 1 2 3 4
[]FMT 2 3 rho iota 6
[]FMT 1 2 3
rho []FMT 2 3 rho 1.5 2 3 4 5 6
# errors
('''Q5'' []FMT 1' []EA '[]DM')
('''I5'' []FMT ''abc''' []EA '[]DM')
('''A5'' []FMT 1 2' []EA '[]DM')
('''I5'' []FMT (1 2) (1 2 3)' []EA '[]DM')
('''I5'' []FMT 1J2' []EA '[]DM')
('''F5.1'' []FMT 1e20' []EA '[]DM')
