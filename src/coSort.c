// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: http://www.viva64.com

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#define _XOPEN_SOURCE /* Mac compatibility. */
#define _BSD_SOURCE

#include "coSort.h"
#include "stack.h"
#include "stackArrays.h"
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define STACK_SIZE (SIGSTKSZ + 512 * 1024)
#define MICDIV 1000000
#define BUF_SIZE 256

static CoPlanner planner;

Array merge(Array a, Array b);

void processFile(int id);

void printResults();

size_t fileLength(const char *file);

int main(int argc, const char **argv) {
    struct timeval latency = {0, 0};
    CoPlanner_init(&planner, argc, latency);

    for (int i = 0; i < argc - 1; i++) {
        CoPlanner_add(&planner, STACK_SIZE, processFile);
        planner.data[i].userData.file = argv[i + 1];
        planner.data[i].userData.count = 0;
        planner.data[i].userData.array = NULL;
    }
    printf("Starting coroutines...\n");
    CoPlanner_fire(&planner);

    printResults();

    stackArray mergeStack;
    StackArray_init(&mergeStack, (int)planner.count + 1);

    for(int i = 0; i < planner.count; i++) {
        Array new = {planner.data[i].userData.array, planner.data[i].userData.count};
        StackArray_push(&mergeStack, new);
    }

    while(StackArray_size(&mergeStack) > 1) {
        Array a = StackArray_pop(&mergeStack);
        Array b = StackArray_pop(&mergeStack);
        StackArray_push(&mergeStack, merge(a, b));
    }
    Array res = {0,0};
    if (StackArray_size(&mergeStack) == 1)
        res = StackArray_pop(&mergeStack);

    FILE* outFile = fopen("out.txt", "w");
    if (!outFile){
        printf("Can't open out.txt");
        return (EXIT_FAILURE);
    }
    for(int i = 0; i < res.count; i++)
        fprintf(outFile, "%d ", ((int*)res.array)[i]);


    if (res.array)
        free(res.array);
    free(mergeStack.items);
    fclose(outFile);
    CoPlanner_destroy(&planner);
    return 0;
}

void mergeSortMerge(int arr[], size_t l, size_t m, size_t r) {
    size_t i, j, k;
    size_t n1 = m - l + 1;
    size_t n2 = r - m;

    int L[n1], R[n2];

    for (i = 0; i < n1; i++) {
        L[i] = arr[l + i];
        CoPlanner_rollIfLatency(&planner);
    }
    for (j = 0; j < n2; j++) {
        R[j] = arr[m + 1 + j];
        CoPlanner_rollIfLatency(&planner);
    }

    i = 0;
    j = 0;
    k = l;
    while (i < n1 && j < n2) {
        CoPlanner_rollIfLatency(&planner);
        if (L[i] <= R[j]) {
            arr[k] = L[i];
            i++;
        } else {
            arr[k] = R[j];
            j++;
        }
        k++;
    }

    while (i < n1) {
        CoPlanner_rollIfLatency(&planner);
        arr[k] = L[i];
        i++;
        k++;
    }

    while (j < n2) {
        CoPlanner_rollIfLatency(&planner);
        arr[k] = R[j];
        j++;
        k++;
    }
}

void mergeSort(int *arr, size_t l, size_t r) {
    if (l < r) {
        CoPlanner_rollIfLatency(&planner);
        size_t m = l + (r - l) / 2;

        mergeSort(arr, l, m);
        CoPlanner_rollIfLatency(&planner);
        mergeSort(arr, m + 1, r);
        CoPlanner_rollIfLatency(&planner);
        mergeSortMerge(arr, l, m, r);
        CoPlanner_rollIfLatency(&planner);
    }
}


void processFile(int id) {
    stack input = {};
    Stack_init(&input, 1000);
    ContextData *nowData = CoPlanner_dataNow(&planner);

    struct aiocb aioreq = {};

    aioreq.aio_fildes = open(nowData->userData.file, O_RDWR);
    aioreq.aio_offset = 0;
    aioreq.aio_nbytes = fileLength(nowData->userData.file);
    char* buffer = calloc(aioreq.aio_nbytes + 5, 1);
    aioreq.aio_buf = buffer;
    if (aioreq.aio_fildes == -1){
        printf("Unable to open %s\n", nowData->userData.file);
        CoPlanner_finishCoroutine(&planner);
        return;
    }

    if (aio_read(&aioreq) == -1)
        handleError("aio_read");


    while (aio_error(&aioreq) == EINPROGRESS)
        CoPlanner_roll(&planner);

    int res  = aio_return(&aioreq);
    if (res != aioreq.aio_nbytes){
        handleError("AIO failed");
    }
    int number = -1;
    const char* ptrChar = buffer;
    for (int i = 0; i < res; i ++){
        if (ptrChar[i] == ' ') {
            Stack_push(&input, number);
            number = -1;
            continue;
        }
        if (number == -1)
            number = 0;
        number *= 10;
        number += ptrChar[i] - '0';
    }
    if (number != -1)
        Stack_push(&input, number);
    free(buffer);

    nowData->userData.count = Stack_size(&input);
    nowData->userData.array = input.items;

    size_t n = nowData->userData.count;
    int *arr = nowData->userData.array;

    mergeSort(arr, 0, n - 1);
    CoPlanner_finishCoroutine(&planner);
}

size_t fileLength(const char *file) {
    FILE* fileob = fopen(file, "rb");
    fseek(fileob, 0L, SEEK_END);
    size_t size = ftell(fileob);
    fclose(fileob);
    return size;
}

Array merge(Array a, Array b) {
    int* new = malloc((a.count + b.count) * sizeof(int));
    int* aC = a.array;
    int* bC = b.array;

    int j = 0, k = 0;
    for (int i = 0; i < a.count + b.count;) {
        if (j < a.count && k < b.count) {
            if (aC[j] < bC[k]) {
                new[i] = aC[j];
                j++;
            }
            else {
                new[i] = bC[k];
                k++;
            }
            i++;
        }
        else if (j == a.count) {
            for (; i < a.count + b.count;) {
                new[i] = bC[k];
                k++;
                i++;
            }
        }
        else {
            for (; i < a.count + b.count;) {
                new[i] = aC[j];
                j++;
                i++;
            }
        }
    }

    free(a.array);
    free(b.array);
    Array ret = {new, a.count + b.count};
    return ret;
}

void printResults() {
    for (int i = 0; i < planner.count; ++i) {
        printf("Switched (id %d) %zu times, total coroutine time: %ld.%06ld\n", i, planner.data[i].switched,
               (long int) planner.data[i].elapsed.tv_sec, (long int) planner.data[i].elapsed.tv_usec);
    }
    struct timeval tval_result;
    timersub(&planner.finish, &planner.entry, &tval_result);
    printf("Total time: %ld.%06ld, %zu switches\n", (long int) tval_result.tv_sec, (long int) tval_result.tv_usec,
           planner.switches);

    size_t latencyCalc = tval_result.tv_sec * MICDIV + tval_result.tv_usec;
    if (planner.switches)
        latencyCalc /= planner.switches;
    latencyCalc *= planner.count;
    printf("Real latency: %ld.%06ld\n", (long int) latencyCalc / MICDIV, (long int) latencyCalc % MICDIV);
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