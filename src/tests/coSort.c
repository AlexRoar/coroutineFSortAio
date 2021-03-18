// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: http://www.viva64.com

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#define _XOPEN_SOURCE /* Mac compatibility. */
#define _BSD_SOURCE

#include "coSort.h"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <ucontext.h>
#include <stdio.h>
#include <errno.h>
#include <aio.h>
#include <signal.h>

#define STACK_SIZE (SIGSTKSZ + 512 * 1024)
#define MICDIV 1000000
#define BUF_SIZE 256

static CoPlanner planner;

void processFile(int id);

void printResults();

void sigev_notify_function(int sig, siginfo_t *si, void *ucontext) {
    write(STDOUT_FILENO, "I/O completion signal received\n", 31);
    printf("%p\n", si->si_value.sival_ptr);
    ucontext_t now;
}

void nf(union sigval v){
    printf("NF\n");
}
struct aiocb* aiocb;
int main(int argc, const char **argv) {
    write(STDOUT_FILENO, "main\n", 5);
    struct sigaction sa = {};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sigev_notify_function;
    if (sigaction(SIGINFO, &sa, NULL) == -1){
        handleError("sigaction");
    }

    aiocb = calloc(1, sizeof (struct aiocb));

    memset(aiocb, 0, sizeof(struct aiocb));
    aiocb->aio_fildes = open("data/1.txt", O_RDONLY);
    aiocb->aio_buf = malloc(100);
    aiocb->aio_nbytes = 100;
    aiocb->aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    aiocb->aio_sigevent.sigev_signo = SIGINFO;
    aiocb->aio_sigevent.sigev_notify_function = nf;
    aiocb->aio_sigevent.sigev_value.sival_ptr = aiocb;

    ucontext_t now;
    getcontext(&now);

    printf("%p\n", now.uc_stack.ss_sp);
    if(aio_read(aiocb) == -1){
        handleError("NOOO");
    }

    printf("OKMAIN\n");
    sleep(5);

    return 0;
    struct timeval l = {0,10};
    CoPlanner_init(&planner, 10, l);
    CoPlanner_add(&planner, STACK_SIZE, processFile);
    CoPlanner_add(&planner, STACK_SIZE, processFile);
    CoPlanner_fire(&planner);
    return EXIT_SUCCESS;
}

void processFile(int id) {
    printf("in co\n");

    struct aiocb* aiocb = calloc(1, sizeof (struct aiocb));

    memset(aiocb, 0, sizeof(struct aiocb));
    aiocb->aio_fildes = open("data/1.txt", O_RDONLY);
    aiocb->aio_buf = malloc(100);
    aiocb->aio_nbytes = 100;
    aiocb->aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    aiocb->aio_sigevent.sigev_signo = SIGUSR2;
    aiocb->aio_sigevent.sigev_value.sival_ptr = aiocb;

    ucontext_t now;
    getcontext(&now);

    printf("%p\n", now.uc_stack.ss_sp);
    if(aio_read(aiocb) == -1){
        handleError("NOOO");
        CoPlanner_finishCoroutine(&planner);
        return;
    }

    printf("OK\n");
    sleep(3);
    CoPlanner_finishCoroutine(&planner);
}


void CoPlanner_init(CoPlanner *this, unsigned noCon, struct timeval latency) {
    this->capacity = noCon;
    this->latency = latency;
    this->latencyByN = latency;
    this->switches = 0;
    this->count = 0;
    this->now = -1;
    this->contexts = malloc(noCon * sizeof(ucontext_t));
    this->data = malloc(noCon * sizeof(ContextData));
    this->active = calloc(noCon, sizeof(char));
    if (!this->contexts || !this->data || !this->active)
        handleError("malloc");
}

void CoPlanner_destroy(CoPlanner *this) {
    free(this->active);
    free(this->data);
    free(this->contexts);
}

void CoPlanner_add(CoPlanner *this, size_t stackSize, void *func) {
    if (this->count >= this->capacity)
        handleError("capacity overflow");
    ucontext_t *newCont = this->contexts + this->count;
    char *stack = allocateStack(stackSize);
    if (getcontext(newCont) == -1)
        handleError("getcontext");

    newCont->uc_stack.ss_sp = stack;
    newCont->uc_stack.ss_size = stackSize;
    newCont->uc_link = &this->main;

    makecontext(newCont, func, 1, this->count);
    this->count++;
}

bool CoPlanner_isActive(CoPlanner *this) {
    for (unsigned i = 0; i < this->count; i++)
        if (this->active[i])
            return true;
    return false;
}

void CoPlanner_fire(CoPlanner *this) {
    if (this->count == 0)
        return;

    struct timeval zeroed = {0, 0};
    for (unsigned i = 0; i < this->count; i++) {
        this->active[i] = true;
        this->data[i].switched = 0;
        this->data[i].elapsed = zeroed;
    }

    CoPlanner_setLatencyN(this);

    this->switches = 0;
    this->entry = getNowFastTime();

    while (CoPlanner_isActive(this)) {
        this->now = CoPlanner_nextAvailable(this);
        CoPlanner_swapToNowFrom(this, &this->main);
    }
    this->finish = getNowFastTime();
    this->now = -1;
}

bool CoPlanner_roll(CoPlanner *this) {
    CoPlanner_addCoElapsed(this);
    if (this->now >= this->count)
        return false;
    ucontext_t *enterC = &this->contexts[this->now];
    this->now = CoPlanner_nextAvailable(this);
    if (this->now >= this->count)
        return false;
    CoPlanner_swapToNowFrom(this, enterC);
    return true;
}

void CoPlanner_swapToNowFrom(CoPlanner *this, ucontext_t *enterC) {
    this->coEntered = getNowFastTime();
    this->data[this->now].switched++;
    this->switches++;
    swapcontext(enterC, &this->contexts[this->now]);
}

bool CoPlanner_rollIfLatency(CoPlanner *this) {
    struct timeval elapsed = CoPlanner_elapsed(this);
    if (elapsed.tv_sec > this->latencyByN.tv_sec || (elapsed.tv_sec == this->latencyByN.tv_sec && elapsed.tv_usec >= this->latencyByN.tv_usec))
        return CoPlanner_roll(this);
    return false;
}

unsigned CoPlanner_nextAvailable(CoPlanner *this) {
    if (this->now >= this->count)
        return 0;
    unsigned next = this->now + 1;
    next %= (unsigned) (this->count);
    for (unsigned i = 0; i < this->count && !this->active[next]; i++) {
        next++;
        next %= this->count;
    }
    if (!this->active[next])
        return -1;
    return next;
}

ContextData *CoPlanner_dataNow(CoPlanner *this) {
    if (this->now >= this->count)
        return NULL;
    return this->data + this->now;
}

ContextData *CoPlanner_dataIth(CoPlanner *this, unsigned i) {
    if (i >= this->count)
        return NULL;
    return this->data + i;
}

void CoPlanner_finishCoroutine(CoPlanner *this) {
    this->active[this->now] = false;
    CoPlanner_addCoElapsed(this);
}

struct timeval CoPlanner_elapsed(CoPlanner *this) {
    struct timeval tval_now, tval_result;
    tval_now = getNowFastTime();
    timersub(&tval_now, &this->coEntered, &tval_result);
    return tval_result;
}

void CoPlanner_setLatencyN(CoPlanner *this) {
    this->latencyByN = this->latency;
    size_t microSum = (this->latency.tv_sec * MICDIV) + this->latency.tv_usec;
    microSum /= this->count;

    this->latencyByN.tv_sec = microSum / MICDIV;
    this->latencyByN.tv_usec = microSum % MICDIV;
}

void CoPlanner_addCoElapsed(CoPlanner *this) {
    struct timeval elapsedNew = CoPlanner_elapsed(this);
    size_t total = this->data[this->now].elapsed.tv_sec * MICDIV + this->data[this->now].elapsed.tv_usec;
    total += elapsedNew.tv_sec * MICDIV + elapsedNew.tv_usec;
    this->data[this->now].elapsed.tv_sec = total / MICDIV;
    this->data[this->now].elapsed.tv_usec = total % MICDIV;
}

struct timeval getNowFastTime() {
    struct timeval out = {};
    gettimeofday(&out, NULL); // Эта строчка занимает 55% времени
    return out;
}

static void *allocateStack(size_t size) {
    void *stack = malloc(size);
    mprotect(stack, size, PROT_READ | PROT_WRITE | PROT_EXEC);
    return stack;
}

#pragma clang diagnostic pop