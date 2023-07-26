#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>
#include <x86gprintrin.h>

#define __NR_uintr_register_handler 449
#define __NR_uintr_unregister_handler 450
#define __NR_uintr_create_fd 451
#define __NR_uintr_register_sender 452
#define __NR_uintr_unregister_sender 453
#define __NR_uintr_wait 454
#define __NR_uintr_event 455

/* For simiplicity, until glibc support is added */
#define uintr_register_handler(handler, flags) syscall(__NR_uintr_register_handler, handler, flags)
#define uintr_unregister_handler(flags) syscall(__NR_uintr_unregister_handler, flags)
#define uintr_create_fd(vector, flags) syscall(__NR_uintr_create_fd, vector, flags)
#define uintr_register_sender(fd, flags) syscall(__NR_uintr_register_sender, fd, flags)
#define uintr_unregister_sender(fd, flags) syscall(__NR_uintr_unregister_sender, fd, flags)
#define uintr_wait(usec, flags) syscall(__NR_uintr_wait, usec, flags)
#define uintr_event(fd) syscall(__NR_uintr_event, fd)

typedef struct Arguments
{
    int size;
    int count;
} Arguments;

typedef unsigned long bench_t;
typedef struct Benchmarks
{
    int count;
    bench_t total_start;  // Start of the total benchmarking
    bench_t single_start; // Start of single benchmark
    bench_t minimum;      // Minimum time
    bench_t maximum;      // Maximum time
    bench_t sum;          // Sum (for averaging)
    bench_t squared_sum;  // Squared sum (for standard deviation)
} Benchmarks;

#define SHM_SIZE (3 * sizeof(unsigned long))
int pipefd[2];
int event_fd = 0; // eventfd
int uintr_fd = 0;

bench_t now() {
	struct timespec ts;
	timespec_get(&ts, TIME_UTC);
	return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

void setup_benchmarks(Benchmarks* bench) {
	bench->count = 0;
	bench->minimum = INT32_MAX;
	bench->maximum = 0;
	bench->sum = 0;
	bench->squared_sum = 0;
	bench->total_start = now();
}

void benchmark(Benchmarks* bench, bench_t now) {
	bench->count++;
    const bench_t time = now - bench->single_start;
    if (time < bench->minimum) {
		bench->minimum = time;
	}
	if (time > bench->maximum) {
		bench->maximum = time;
	}
	bench->sum += time;
	bench->squared_sum += (time * time);
}

void evaluate(Benchmarks* bench, Arguments* args) {
	assert(args->count > 0);
	// bench_t total_time = now() - bench->total_start;
	double average = (bench->sum) / bench->count;

	double sigma = bench->squared_sum / bench->count;
	sigma = sqrt(sigma - (average * average));

	int messageRate = (int)(bench->count / (bench->sum / 1000000000.0));

	printf("\n============ RESULTS ================\n");
	printf("Message size:       %d\n", args->size);
	printf("Message count:      %d\n", bench->count);
	printf("Total duration:     %.3f\tms\n", bench->sum / 1000000.0);
	printf("Average duration:   %.3f\tus\n", average / 1000.0);
	printf("Minimum duration:   %.3f\tus\n", bench->minimum / 1000.0);
	printf("Maximum duration:   %.3f\tus\n", bench->maximum / 1000.0);
	printf("Standard deviation: %.3f\tus\n", sigma / 1000.0);
	printf("Message rate:       %d\tmsg/s\n", messageRate);
	printf("=====================================\n");
}

void flag_wait(atomic_bool *flag)
{
    while (atomic_load(flag) != 1)
        ;
    atomic_store(flag, 0);
}

void read_start_wait(atomic_ulong *read_start)
{
    while (atomic_load(read_start) != 0)
        ;
}

void flag_notify(atomic_bool *flag)
{
    atomic_store(flag, 1);
}

void flag_clear(atomic_bool *flag)
{
    atomic_store(flag, 0);
}

// uintr 接收方中断处理函数
void __attribute__((interrupt)) __attribute__((target("general-regs-only", "inline-all-stringops")))
uintr_handler(struct __uintr_frame *ui_frame, unsigned long long vector)
{
    // printf("uintr_handler\n");
}

void writer(int event_fd, int uintr_fd, Arguments *args)
{
    int uipi_index; // 发送方 uipi 索引
    int shmid;      // 共享内存标识符
    char *shm;      // 共享内存指针

    // 接收 uintr_fd
    close(pipefd[1]); // 关闭写端
    if (read(pipefd[0], &uintr_fd, sizeof(uintr_fd)) == -1)
    {
        perror("read");
        exit(EXIT_FAILURE);
    }

    uipi_index = uintr_register_sender(uintr_fd, 0); // 注册发送方
    if (uipi_index < 0)
    {
        printf("Sender register error\n");
        exit(EXIT_FAILURE);
    }

    // 共享内存
    key_t key = ftok(".", 'a'); // 生成共享内存的键值
    shmid = shmget(key, SHM_SIZE, IPC_CREAT | 0666);
    if (shmid == -1)
    {
        perror("shmget");
        exit(1);
    }
    shm = shmat(shmid, NULL, 0);

    atomic_bool *flag = (atomic_bool *)shm; // flag
    flag_clear(flag);
    atomic_ulong *read_start = (atomic_ulong *)(shm + sizeof(atomic_bool)); // 写者开始时间

    _stui(); // 开启 uintr 中断

    for (int i = 0; i < args->count; i++)
    {
        // printf("writer: %d\n", i);
        read_start_wait(read_start); // 等待时间清零
        flag_wait(flag);
        // 写入开始时间
        atomic_store(read_start, now()); // 写者通知时间
        // Notify the reader
        _senduipi(uipi_index); // 发送 uintr
        flag_clear(flag);
    }

    shmdt(shm);                           // 删除共享内存
    uintr_unregister_sender(uintr_fd, 0); // 注销发送方
}

void reader(int event_fd, Arguments *args, char *shm)
{
    struct Benchmarks bench;
    int epollfd;                  // 文件描述符
    struct epoll_event event;     // epoll 事件结构体
    struct epoll_event events[2]; // epoll 事件数组

    // 创建 epoll
    epollfd = epoll_create1(0);
    // 添加 fd 到 epoll 中
    event.data.fd = event_fd;
    event.events = EPOLLIN; // 可读事件
    epoll_ctl(epollfd, EPOLL_CTL_ADD, event_fd,
              &event); // 添加 eventfd 到 epoll 中

    atomic_bool *flag = (atomic_bool *)shm;
    flag_clear(flag);
    uint64_t val;
    atomic_ulong *read_start = (atomic_ulong *)(shm + sizeof(atomic_bool)); // 写者开始时间
    atomic_store(read_start, 0);                                            // read_start 清零

    // 将 uintr_fd 写入管道 | 方便同步一下 write 状态
    close(pipefd[0]); // 关闭读端
    if (write(pipefd[1], &uintr_fd, sizeof(uintr_fd)) == 100)
    {
        perror("write");
        exit(EXIT_FAILURE);
    }

    _stui(); // 开启 uintr 中断
    setup_benchmarks(&bench);
    bench_t temp = 0;

    for (int i = 0; i < args->count; i++)
    {
        // printf("reader: %d\n", i);
        atomic_store(read_start, 0);
        flag_notify(flag);
        int num_events = epoll_wait(epollfd, events, 5, 500);
        if (num_events == -1) // epoll_wait 出错
        {
            perror("epoll_wait");
            exit(1);
        }
        // Read eventfd
        if (read(event_fd, &val, sizeof(uint64_t)) == -1)
        {
            perror("read");
            // exit(1);
        }
        else
        {
            bench.single_start = atomic_load(read_start);
            // 如果写者开始时间到现在 > 1s
			temp = now();
            // 如果写者开始时间到现在 < 0.5s
			if (temp - bench.single_start < 500000000)
			{
                benchmark(&bench, temp);
            }
        }
    }
    // benchmark(&bench); // 最后一个可能是错误,舍弃
    args->size = 1; // 消息长度 1
    evaluate(&bench, args);

    // 删除 epoll 和 fd
    close(uintr_fd);
    uintr_unregister_handler(0);
}

int communicate(Arguments *args)
{

    // 注册中断处理函数 | 仅占位
    if (uintr_register_handler(uintr_handler, 0))
    {
        printf("Interrupt handler register error\n");
        exit(EXIT_FAILURE);
    }
    // uintr_fd
    uintr_fd = uintr_create_fd(0, 0); // 创建 uintr 文件描述符
    if (uintr_fd < 0)
    {
        printf("Interrupt vector allocation error\n");
        exit(EXIT_FAILURE);
    }

    // 创建 eventfd
    event_fd = eventfd(0, EFD_NONBLOCK);
    if (event_fd == -1)
    {
        perror("eventfd");
        exit(1);
    }
    uintr_event(event_fd); // 注册 eventfd

    // 创建管道
    if (pipe(pipefd) == -1)
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    char *shm;
    // 创建共享内存
    key_t key = ftok(".", 'a'); // 生成共享内存的键值
    int shmid = shmget(key, SHM_SIZE, IPC_CREAT | 0666);
    if (shmid == -1)
    {
        perror("shmget");
        exit(1);
    }
    shm = shmat(shmid, NULL, 0);

    pid_t pid = fork();
    if (pid == 0)
    { // 子进程（写进程）
        writer(event_fd, uintr_fd, args);
        exit(0);
    }
    // 父进程（读进程）
    reader(event_fd, args, shm);

    // 删除共享内存
    shmdt(shm); // 删除共享内存
    shmctl(shmid, IPC_RMID, NULL);
    return 0;
}

int main(int argc, char *argv[])
{
    struct Arguments args = {
        .size = 1,
        .count = 100000,
    };
    communicate(&args);
    return EXIT_SUCCESS;
}