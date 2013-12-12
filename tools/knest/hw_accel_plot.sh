grep hw= err | cut -f2,3 -d'=' | sed "s/. klee=/ /g" | awk '{ x = x + $1; y = y + $2; print x " " y; }' >hwaccel.dat
gnuplot <<<"
set terminal png
set output 'hwaccel.png'
set xlabel 'nth Syscall'
set ylabel 'Time (Seconds)'
set title 'Cumulative Hardware and Interpreter Time'
set y2label 'Speedup'
set y2tics
plot 'hwaccel.dat' using 0:1 title 'Hardware' with lines, '' using 0:2 title 'Interpreter' with lines,\
	'' using 0:("'$2/$1'") with lines title 'Speedup' axes x1y2, 1 title '' axes x1y2

set terminal postscript enhanced color
set output 'hwaccel-dat.eps'
replot 
"
