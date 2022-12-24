# NJUOSM2 - libco

**实验手册->:[M2: 协程库 (libco) (jyywiki.cn)](http://jyywiki.cn/OS/2022/labs/M2)
基础代码位于（M2分支下）：
https://github.com/NJU-ProjectN/os-workbench-2022**

#### Makefile约定操作：
在`./libco `目录下 可使用`make all` 编译生成`libco-32.so`和`libco-64.so`两个共享库。
在`./libco/tests`目录下存放测试代码，`make test`可进行32位和64位程序的测试。具体编译和生成请查看前面的**手册**捏。

### 本实验解决的问题：
**我们有没有可能在不借助操作系统 API (也不解释执行代码) 的前提下，用一个进程 (状态机) 去模拟多个共享内存的执行流 (状态机)？**

### 实验描述
在这个实验中，我们实现轻量级的用户态[协程](https://en.wikipedia.org/wiki/Coroutine) (coroutine，“协同程序”)，也称为 green threads、user-level threads，可以在一个不支持线程的操作系统上实现共享内存多任务并发。即我们希望实现 C 语言的 “函数”，它能够：
-   被 `start()` 调用，从头开始运行；
-   在运行到中途时，调用 `yield()` 被 “切换” 出去；
-   稍后有其他协程调用 `yield()` 后，选择一个先前被切换的协程继续执行。

### 实验要求：
实现这几个api：
1.  `co_start(name, func, arg)` 创建一个新的协程，并返回一个指向 `struct co` 的指针 (类似于 `pthread_create`)。
    -   新创建的协程从函数 `func` 开始执行，并传入参数 `arg`。新创建的协程不会立即执行，而是调用 `co_start` 的协程继续执行。
    -   使用协程的应用程序不需要知道 `struct co` 的具体定义，因此请把这个定义留在 `co.c` 中；框架代码中并没有限定 `struct co` 结构体的设计，所以你可以自由发挥。
    -   `co_start` 返回的 `struct co` 指针需要分配内存。我们推荐使用 `malloc()` 分配。
2.  `co_wait(co)` 表示当前协程需要等待，直到 `co` 协程的执行完成才能继续执行 (类似于 `pthread_join`)。
    -   在被等待的协程结束后、 `co_wait()` 返回前，`co_start` 分配的 `struct co` 需要被释放。如果你使用 `malloc()`，使用 `free()` 释放即可。
    -   因此，每个协程只能被 `co_wait` 一次 (使用协程库的程序应当保证除了初始协程外，其他协程都必须被 `co_wait` 恰好一次，否则会造成内存泄漏)。
3.  `co_yield()` 实现协程的切换。协程运行后一直在 CPU 上执行，直到 `func` 函数返回或调用 `co_yield` 使当前运行的协程暂时放弃执行。`co_yield` 时若系统中有多个可运行的协程时 (包括当前协程)，你应当随机选择下一个系统中可运行的协程。
4.  `main` 函数的执行也是一个协程，因此可以在 `main` 中调用 `co_yield` 或 `co_wait`。`main` 函数返回后，无论有多少协程，进程都将直接终止。

我们会发现其实协程跟线程很像，它本身就是“用户态线程”（不经过os提供的api或系统调用），前提是你使用协程库的代码每句后面加上`co_yeild`。因为线程的调度不是由线程决定的 (由操作系统和硬件决定)，但协程除非执行 `co_yield()` 主动切换到另一个协程运行，当前的代码就会一直执行下去。

## 思路与心得

#### 1.理解`co_yeild`要做什么并调试一下`setjmp`和`longjmp`
>`co_yield` 要做的就是**把 yield 瞬间的 callee preserved registers() “封存” 下来** (其他寄存器按照 ABI 约定，`co_yield` 既然是函数，就可以随意摧毁)，然后执行堆栈 (rsp) 和执行流 (rip) 的切换：
>`co_yield` 返回时，必须保证**rbx, rsp, rbp, r12, r13, r14, r15**的值和调用时保持一致 (这些寄存器称为 callee saved/non-volatile registers/call preserved, 和 caller saved/volatile/call-clobbered 相反)。
>为什么是 callee preserved registers?
>
>因为我们要保证调用co_yeild()的协程A（线程）在回到该线程的执行流时**被调用者保存的寄存器**被保护不变。因此先封存住，然后切出去执行另一个B（或自己）的协程。然后恢复选择的这个协程的上下文（callee preserved registers），换到它的执行流。这时候相当于B调用的co_yeild()返回。

~~协程的数据结构就是用手册给出的参考结构：~~，加了个next指针
```C
struct co
{
    struct co *next;
    void (*func)(void *);
    void *arg;
    char name[50];
    enum co_status status;         // 协程的状态
    struct co *waiter;             // 是否有其他协程在等待当前协程
    jmp_buf context;               // 寄存器现场 (setjmp.h)
    uint8_t stack[STACK_SIZE + 1]; // 协程的堆栈                        // uint8_t == unsigned char
};
```

**那怎么实现寄存器现现场切换？**
使用 C 语言标准库中的 `setjmp/longjmp` 函数来实现寄存器现场的保存和恢复。
当 选择的协程是调用 `yield()` 切换出来的，此时该协程已经调用过 `setjmp` 保存寄存器现场，我们直接 `longjmp` 恢复寄存器现场即可。
`setjmp和longjmp`做了什么？debug！！
![Pasted image 20221001231656.png](https://s2.loli.net/2022/10/02/kUhW2TV7yNO6uE1.png)
经过一些简易跳转来到这里：
![Pasted image 20221001231751.png](https://s2.loli.net/2022/10/02/75iBTJxFQgPZ9cs.png)
我们可以看到在`$rdi`的内存buffer上保存先保存`$rbx`、然后将`$rbp`移到`rax`经过一些计算保存到`rbx`后面，接着是`$r12 $r13 $r14 $r15`,再接着将`$rsp+8`（+8因为经历一次函数call，rsp-8了） 移到`$rdx`经过计算存入内存。

我们来看`longjmp`：
![Pasted image 20221001232718.png](https://s2.loli.net/2022/10/02/ZwIXbAq6hyCldGH.png)
无事发生，再度跳转
![Pasted image 20221001233555.png](https://s2.loli.net/2022/10/02/MEfY2ijyBPF8ldL.png)
很明显了，先从buffer中恢复setjmp存的rsp和rbp，经过一些逆向计算，存到r8和r9中。
![Pasted image 20221001233625.png](https://s2.loli.net/2022/10/02/kREFDC4KqiTpULg.png)
67行跳转到了这里：
![Pasted image 20221001233902.png](https://s2.loli.net/2022/10/02/xoylAOvDuUEesVw.png)
很明显，恢复rbx,r12,r13,r14,r15。然后将逆向计算还原的rsp和rbp从r8和r9中取出，恢复，结束。

#### 2.切换堆栈
每当 `co_yield()` 发生时，我们都会选择一个协程继续执行，此时必定为以下两种情况之一：
1.  选择的协程是新创建的，此时该协程还没有执行过任何代码，只是在`start`中创建了，我们需要首先执行切换堆栈，然后开始执行这个协程的代码；
2.  选择的协程是调用 `yield()` 切换出来的，此时该协程已经调用过 `setjmp` 保存寄存器现场，我们直接 `longjmp` 恢复寄存器现场即可。

#### 如何切换堆栈？
首先需要知道内联汇编的一些知识：
[GCC-Inline-Assembly-HOWTO](http://www.ibiblio.org/gferg/ldp/GCC-Inline-Assembly-HOWTO.html)

然后这里是很多bug和失败的原因。
```C
static inline void stack_switch_call(void *sp, void *entry, uintptr_t arg) {
  asm volatile (
#if __x86_64__
    "movq %0, %%rsp; movq %2, %%rdi; jmp *%1"
      : : "b"((uintptr_t)sp), "d"(entry), "a"(arg) : "memory"
#else
    "movl %0, %%esp; movl %2, 4(%0); jmp *%1"
      : : "b"((uintptr_t)sp - 8), "d"(entry), "a"(arg) : "memory"
#endif
  );
}
```
首先手册里的指导代码是一个**提示**，但并不是**在本实验**可以正确运行的，因为手册里的这个代码是OS Lab里`abstract-machine`切换堆栈用的，它是为进程服务的，他当然不需要返回。因为你直接`jmp`过去了自然没法返回。进程执行完不需要返回，**但是我们的协程需要**，不然你没法`wait`一个协程结束。

我的实现：
```C
static inline void stack_switch_call(void *sp, void *entry, uintptr_t arg)
{
    asm volatile(
#if __x86_64__
        "movq %%rsp,-0x10(%0); leaq -0x20(%0), %%rsp; movq %2, %%rdi ; call *%1; movq -0x10(%0) ,%%rsp;"
        :
        : "b"((uintptr_t)sp), "d"(entry), "a"(arg)
        : "memory"
#else
        "movl %%esp, -0x8(%0); leal -0xC(%0), %%esp; movl %2, -0xC(%0); call *%1;movl -0x8(%0), %%esp"
        :
        : "b"((uintptr_t)sp), "d"(entry), "a"(arg)
        : "memory"
#endif
    );
}
```
解释一下：我使用了call。在call之前将rsp转到了传递进来的参数（co数据结构中为每个协程分配的堆栈）上。因此在call前要保存现在的rsp。
然后注意：
>x86-64 要求堆栈按照 16 字节对齐 (x86-64 的堆栈以 8 字节为一个单元)，这是为了确保 SSE 指令集中 XMM 寄存器变量的对齐。如果你的程序遇到了神秘的 Segmentation Fault (可能在某个 libc 的函数中)，如果你用 gdb 确定到 Segmentation Fault 的位置，而它恰好是一条 SSE 指令，例如
`movaps %xmm0,0x50(%rsp) movaps %xmm1,0x60(%rsp) ...`
那很可能就是你的堆栈没有正确对齐。我们故意没有说的是，System V ABI (x86-64) 对堆栈对齐的要求，是在 “何时” 做出的——在 `call` 指令之前按 16 字节对齐，在 `call` 指令之后就不对齐了。一方面你可以暴力地尝试一下；如果你想更深入地理解这个问题，就需要读懂 `stack_switch_call`，以及 STFW 关于 ABI 对对齐的要求，或是查看编译出的汇编代码。

因此你要确保`leaq -0x20(%0), %%rsp;`传递的是16字节对齐的，32位程序8字节对齐。

还有：
栈是向小地址生长的，所以一开始要为这个函数传入高地址！！！
`stack_switch_call(&current->stack[STACK_SIZE], current->func, (uintptr_t)current->arg);`
否则就等着段错误😛吧。而且这个高地址要确保是16的倍数呢！！！！

什么时候协程结束？
```C
((struct co volatile *)current)->status = CO_RUNNING; //  fogot!!!
stack_switch_call(&current->stack[STACK_SIZE], current->func, (uintptr_t)current->arg);
((struct co volatile *)current)->status = CO_DEAD;
if (current->waiter)
   current = current->waiter;
```
stack_switch_call()执行完，代表你call的entry里的代码实行结束返回了。标记该协程执行完，切换回正在等待的协程。在co_wait里回收。

#### 3. 一些细节与BUG！！！
1. 默认手册给出的` struct co` 中 `char* name`  没有分配内存
2.  `current`    一开始 `位于main函数中` 未初始化呀
3. ` current->status = CO_RUNNING; // BUG !! 写成了 current->status==CO_RUNNING;`
在复制粘贴中将`=` 默认成了`==`，寄！还很隐蔽，这也是vscode的弊端，在ide中一般可以提示出来。
4. 调度问题！！！！！
这会导致一些奇奇怪怪的BUG！！！还很难调试出来，甚至有时候加个`printf`后者在不会执行的地方加一个函数调用就消失了，amazing！

一开始我的调度：
粗暴数组随机数，所有协程保存在一个数组。每次调度时，在0-n（当前数组最高索引值）之间随机一个数，调度它。
结果实现的很粗糙，而且回收做起来非常丑陋，数组吗删除得一个个挪，也有严重性能问题（或者里面有bug），抛弃了。

接下来：
想到一个环形链表，比较优雅（自认为）
创建时：
```C
struct co *h = current;
while (h)
    {
     if (h->next == current)
        break;
     h = h->next;
    }
assert(h);
h->next = start;
start->next = current;
return start;
```
释放时：
```C
	struct co *h = current;
    while (h)
    {
        if (h->next == co)
            break;
        h = h->next;
    }
    //从环形链表中删除co
    h->next = h->next->next;
    free(co);
```
但调度变得，就会容易出bug。
一开始的想法是环形链表，直接调度current->next的协程即可。结果出了bug。
因为next指向的协程不一定可以被调度。
```C
	struct co *co_next = current;
        while (co_next->status == CO_DEAD || co_next->status == CO_WAITING);
        {
            co_next = co_next->next;
        }
        current = co_next;
```
修改一下，找到一个可调度的去调度，然后出现了上面说的：
>奇奇怪怪的BUG！！！还很难调试出来，甚至有时候加个`printf`后者在不会执行的地方加一个函数调用就消失了，amazing！
```C
    struct co *thd1 = co_start("producer-1", producer, queue);
    struct co *thd2 = co_start("producer-2", producer, queue);
    struct co *thd3 = co_start("consumer-1", consumer, queue);
    struct co *thd4 = co_start("consumer-2", consumer, queue);
    co_wait(thd1);
    co_wait(thd2);
    g_running = 0;
    co_wait(thd3);
    co_wait(thd4);
```
折腾半天发现，他会在第二个测试，生产者与消费者测试中，
```C
static void producer(void *arg) {//生产者
    Queue *queue = (Queue*)arg;
    for (int i = 0; i < 100; ) {
        if (!q_is_full(queue)) {
            do_produce(queue);
            i += 1;
        }
        co_yield();// failed
    }
}
```
因为在这样的调度中一个producer协程一个劲儿生产在生产满100后。（其他协程不会被调度，这也正是bug所在），这个协程执行100次循环结束，接着另一个producer协程执行， 判断队列是否已满（100）后，因为没有消费者被调度过，第一个生产满了，这时候不进行生产，co_yeild()切出去。但是！！！！在这样的co_yeild调度中，因为这个producer仍旧是running状态。所以它while判断直接出去，仍旧调度这个producer，这个producer因为已经满了，就一直yeild，就死循环了。
原因：
因为这样**极其不公平的调度**导致consumer根本拿不到cpu，就会在producer卡死，因此我们要尽可能会调度到每一个协程，其实微微改一下就行了：
```C
struct co *co_next = current;
        do
        {
            co_next = co_next->next;
        } while (co_next->status == CO_DEAD || co_next->status == CO_WAITING);
        current = co_next;
```
非常小的改动，do-while使它必定会先走一下next，如果next指向的协程可以调度，那就调度到该协程，如果不能，继续换。这样能够在这样的循环链表中不断循环，使每个人都得到公平调度。也符合协程调度yeild()是想让出去的这一意义，所以无论如何，先看看下个协程能否调度可以完美避免（或许吧😰）这个bug。

谢谢你看到这里！结束啦！
