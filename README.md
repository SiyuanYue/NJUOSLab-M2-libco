# NJUOSLab-M2-libco
**实验手册->:[M2: 协程库 (libco) (jyywiki.cn)](http://jyywiki.cn/OS/2022/labs/M2)
基础代码位于（M2分支下）：
https://github.com/NJU-ProjectN/os-workbench-2022**

## 文档: ./doc目录下；代码：./libco目录下

#### Makefile约定操作：
在`./libco `目录下 可使用`make all` 编译生成`libco-32.so`和`libco-64.so`两个共享库。
在`./libco/tests`目录下存放测试代码，`make test`可进行32位和64位程序的测试。具体编译和生成请查看前面的**手册**捏。
### 本实验解决的问题：
**我们有没有可能在不借助操作系统 API (也不解释执行代码) 的前提下，用一个进程 (状态机) 去模拟多个共享内存的执行流 (状态机)？**


