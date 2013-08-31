// |jit-test| slow; debug

/* Make a lot of functions of the form:
function x1(){x1();}
function x2(){x2();}
function x3(){x3();}
...
*/

var s = '';
for (var i = 0; i < 70000; i++) {
    s += 'function x' + i + '() { x' + i + '(); }\n';
}
s += 'trap(0); pc2line(1);\n'
evaluate(s);
