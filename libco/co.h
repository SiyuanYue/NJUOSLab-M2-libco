#define panic_on(cond, s) \
  ({ if (cond) { \
      puts("Panic: "); puts(s); \
      exit(1); \
    } })

#define panic(s) panic_on(1, s)
struct co* co_start(const char *name, void (*func)(void *), void *arg);
void co_yield();
void co_wait(struct co *co);
