编译
g++ *.cpp -std=c++17 -o test

运行
./test

测试
1 查看进程号 
ps uax | grep <name>
2 查看该进程号下所有线程信息
ps -eLf | grep <pid>