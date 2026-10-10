# structure.apl - sorting, searching and set functions
v <- 31 41 59 26 53 58
gradeup v
v[gradeup v]
v[gradedown v]
'abcde' indexof 'dxb'
unique 3 1 3 2 1
(1 2 3 4 5) without 2 4
(1 2 3) union 3 4 5
(1 2 3 4) intersect 3 4 5
1 0 1 0 1 compress 'abcde'
2 0 1 compress 'abc'
1 0 1 0 1 expand 7 8 9
(24 60 60) encode 3725
(24 60 60) decode 1 2 5
1 0 0 1 1 2 partition 'abcdef'
where 0 1 1 0 1
(1 1) find 0 1 1 1 0
2 take iota 5
2 drop iota 5
iota 2 3
