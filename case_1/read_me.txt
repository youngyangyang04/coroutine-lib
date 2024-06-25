案例1 单线程协程模型
在单一线程下，主协程负责调度任务(主协程即调度协程)和子协程负责执行任务

compile using
g++ -std=c++11 *.cpp -o sheduler_test.a