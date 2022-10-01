#include "co.h"
#include <stdlib.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#define CO_SIZE 200

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
enum co_status
{
	CO_NEW = 1,		// 新创建，还未执行过
	CO_RUNNING = 2, // 已经执行过
	CO_WAITING = 3, // 在 co_wait 上等待
	CO_DEAD = 4,	// 已经结束，但还未释放资源
};

#define STACK_SIZE 4 * 1024 * 8 // uintptr_t ---->  8 in x86_64
#define CANARY_SZ 2
#define MAGIC 0x55
struct co
{
	struct co *next;
	void (*func)(void *);
	void *arg;
	char name[50];
	enum co_status status;		   // 协程的状态
	struct co *waiter;			   // 是否有其他协程在等待当前协程
	jmp_buf context;			   // 寄存器现场 (setjmp.h)
	uint8_t stack[STACK_SIZE + 1]; // 协程的堆栈						   // uint8_t == unsigned char
};
// void canary_init(uint8_t ptr[])
// {
// 	for (int i = 0; i < CANARY_SZ; i++)
// 		ptr[i] = ptr[STACK_SIZE - i] = MAGIC;
// }
// void canary_check(uint8_t ptr[], struct co *co)
// {
// 	for (int i = 0; i < CANARY_SZ; i++)
// 	{
// 		if (ptr[i] != MAGIC)
// 			printf("\n %s overflow", co->name);
// 		if (ptr[STACK_SIZE - i] != MAGIC)
// 			printf("\n %s overflow", co->name);
// 	}
// }
struct co *current;
struct co *co_start(const char *name, void (*func)(void *), void *arg)
{
	struct co *start = (struct co *)malloc(sizeof(struct co));
	start->arg = arg;
	start->func = func;
	start->status = CO_NEW;
	strcpy(start->name, name);
	if (current == NULL) // init main
	{
		current = (struct co *)malloc(sizeof(struct co));
		current->status = CO_RUNNING; // BUG !! 写成了 current->status==CO_RUNNING;
		current->waiter = NULL;
		strcpy(current->name, "main");
		current->next = current;
	}
	//环形链表
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
}
int times = 1;
void co_wait(struct co *co)
{
	current->status = CO_WAITING;
	co->waiter = current;
	while (co->status != CO_DEAD)
	{
		co_yield ();
	}
	current->status = CO_RUNNING;
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
}
void co_yield ()
{
	if (current == NULL) // init main
	{
		current = (struct co *)malloc(sizeof(struct co));
		current->status = CO_RUNNING;
		strcpy(current->name, "main");
		current->next = current;
	}
	assert(current);
	int val = setjmp(current->context);
	if (val == 0) // co_yield() 被调用时，setjmp 保存寄存器现场后会立即返回 0，此时我们需要选择下一个待运行的协程 (相当于修改 current)，并切换到这个协程运行。
	{
		// choose co_next
		struct co *co_next = current;
		do
		{
			co_next = co_next->next;
		} while (co_next->status == CO_DEAD || co_next->status == CO_WAITING);
		current = co_next;
		if (co_next->status == CO_NEW)
		{
			assert(co_next->status == CO_NEW);
			((struct co volatile *)current)->status = CO_RUNNING; //  fogot!!!
			stack_switch_call(&current->stack[STACK_SIZE], current->func, (uintptr_t)current->arg);
			((struct co volatile *)current)->status = CO_DEAD;
			if (current->waiter)
				current = current->waiter;
		}
		else
		{
			longjmp(current->context, 1);
		}
	}
	else // longjmp returned(1) ,don't need to do anything
	{
		return;
	}
}
