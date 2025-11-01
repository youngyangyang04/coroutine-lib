# 项目概述

## 为什么要做协程库？

1、**不“烂大街”，主打一个差异化**

现在CPP的项目其实可选的余地并不多，大家主要是做webserver。

2、**协程确实有用**

面试中，面试官经常会问这样的问题，“你知道线程和进程区别吗？”然后会紧接着追问“你了解协程吗？

协程和进程、线程又有什么区别？

”我们通过基础知识的学习和如WebServer此类项目，已经对进程和线程有了比较深的理解，但对协程相关知识却知之甚少。

协程作为一种强大的并发编程技术，可以在需要处理大量I/O操作或者并发任务的情况下提高程序的性能和可维护性。

在许多场景应用广泛，如果我们能做一个协程库的项目，不但可以让简历更加出彩，对以后的工作也大有帮助。

3、**协程库只是一个轮子，可以方便的应用在其他项目中**，增加其他项目的“新意”

自己手动完成一个协程库，还可以直接将我们自己编写的协程库用在其他项目里。

就比如“烂大街”的WebServer，引入协程技术，不但可以提高并发和资源利用率，还大大简化了异步编程的复杂性，使代码更易于理解和维护，这样这个“烂大街”的项目也就有了新意。

4、**增加知识的深度和广度，提高面试通过率**

深入理解了协程技术后，即使面试官不主动问协程技术，就算问进程与线程，我们也可以主动提及协程，与线程和进程对比，引导面试官问协程相关的问题，主动展示自己知识的深度和广度，这会大大提高我们面试的通过率。

### 本项目文档

这次的项目文档依然非常齐全，**从 前置知识 到 理论基础，从 动手实现 到 项目拓展 再到最后 简历如何写，面试会问的问题**，都给大家安排了。

* 前序
    * 为什么要做协程库？
    * 所需要的基础知识
    * 编程语言
    * 操作系统&Linux
    * 计算机网络
    * 参考书籍&开源项目&博客
* **动手前先了解一下协程**
    * 协程基础知识
        * 什么是协程？
        * 对称协程与非对称协程
        * 有栈协程与无栈协程
        * 独立栈与共享栈
        * 协程的优缺点
    * C++有哪些协程库？
* **开始动手**
    * 协程类的实现
    * 协程调度
    * 协程+IO
    * 定时器
    * hook
* **写好了就完了吗**？
    * 项目扩展
    * 协程+
    * 性能测试
* **如何应对面试**？
    * 简历怎么写？
    * 面试会问哪些问题呢？

大家看完这份文档，直接就可以按照文档里的简历写法，写到自己的简历里。

**面试官最喜欢问的项目难点，项目收获**，都给大家列好了：

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241202110429.png' width=500 alt=''></img></div>


面试常见问题也给大家列出来，如果自己没时间理解，直接“背诵”

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241202110524.png' width=500 alt=''></img></div>

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241202110554.png' width=500 alt=''></img></div>

本项目的性能测试很重要，用了协程库为什么性能就能提升，具体提升了多少：

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241202110700.png' width=500 alt=''></img></div>


如果你认真做完项目，本项目文档给你可以优化的方向：

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241202110747.png' width=500 alt=''></img></div>

项目文档其他部分截图：

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241202110916.png' width=500 alt=''></img></div>

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241202110857.png' width=500 alt=''></img></div>

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241202110830.png' width=500 alt=''></img></div>

## 本项目第二版优化

### 性能测试

协程库项目是去年（23年）12月份发布的，已经过去一年了，也有不少录友反馈了一些问题。

主要问题就是性能测试这块，因为面试的时候，面试官经常问，用了协程库 性能究竟提升在哪。

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241210102240.png' width=500 alt=''></img></div>

第二版本，对性能测试这块做了全面的补充，和代码说明 ：

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241210102342.png' width=500 alt=''></img></div>

### 具体代码实现讲解

对项目代码中具体的类都做了详细讲解：

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241210102623.png' width=500 alt=''></img></div>

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241210102740.png' width=500 alt=''></img></div>

在讲解中，把大家可能产生的疑惑，都以面试问答的方式来做讲解：

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241210102849.png' width=500 alt=''></img></div>

对于代码的执行顺序都画详细流程图

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241210103004.png' width=500 alt=''></img></div>

<div align="center"><img src='https://file1.kamacoder.com/i/algo/20241210103153.png' width=500 alt=''></img></div>


## 项目文档获取方式

**本文档仅为星球内部专享，大家可以加入[知识星球](https://www.programmercarl.com/other/project_coroutine.html)里获取，在星球置顶一**。

