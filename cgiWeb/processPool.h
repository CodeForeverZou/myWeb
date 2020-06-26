#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

// 进程池 模板类
// 半同步/半异步的并发模式（连接信号接收、发送都由主进程管理）
// 类的工作：监听消息，创建子进程并分发消息给它处理

// 子进程类（包含它的数据）
class process
{
public:
    process():m_pid(-1){}   // 构造函数
    pid_t m_pid;
    int m_pipefd[2];
};

// 进程池模板
template <typename T>
class processPool
{
private:
    // 为了声明单例模式，需将构造方法声明为私有
    processPool(int listenfd, int process_number = 8);  
    void setup_sig_pipe();
    void run_parent();
    void run_child();
    static const int MAX_PROCESS_NUMBER = 16;
    static const int USER_PER_PROCESS = 65536;  // 每个子进程 最多能处理的客户数
    static const int MAX_EVENT_NUMBER = 10000;  // epoll最多能处理的事件数

    int m_process_number;   // 进程池中 进程总数
    int m_idx;              // 子进程在进程池中的 序号（0开始）
    int m_epollfd;          // epoll事件表
    int m_listenfd;         // 网络连接fd
    int m_stop;             // 子进程 是否停止运行
    process* m_sub_process; // 保存所有 子进程描述信息（是一个数组 new process[]） 
    static processPool<T>* m_instance;  // 只能有一个静态实例
public:
    // processPool(); 单例模式 !!!
    static processPool<T>* create(int listenfd, int process_number = 8){
        if (!m_instance)
            processPool(listenfd, process_number);
        return m_instance;
    }
    ~processPool(){
        delete [] m_sub_process;
    }
    void run();     // 定义执行 过程
};

// 单例模式：对静态变量初始化
template<typename T> 
processPool<T>* processPool<T>:::m_instance = NULL;
// 定义全局的 信号管道
static int sig_pipefd[2];

// -----------------------------------具体方法定义---------------------------------------
// 单例模式：构造方法
template<typename T> 
processPool<T>::processPool(int listenfd, int process_number)
    : m_listenfd(listenfd), m_process_number(process_number), m_idx(-1), m_stop(false)
{
    assert((process_number > 0) && (process_number <= m_process_number));
    m_sub_process = new process[process_number];
    assert(m_sub_process);

    for(int i = 0; i < process_number; i++){
        // 父子进程 建立 管道通信
        int ret = socketpair(PF_UNIX, SOCK_STREAM, m_sub_process[i].m_pipefd);
        assert(ret == 0);
        // 创建 子进程
        m_sub_process[i].m_pid = fork();
        assert(m_sub_process[i].m_pid >= 0);
        if (m_sub_process[i].m_pid > 0){
            close(m_sub_process[i].m_pipefd[1]);    // 关闭读就绪
            continue;
        }
        else{
            close(m_sub_process[i].m_pipefd[0]);    // 关闭读就绪
            m_idx = i;
            break;
        }
    }
}

// 设置fd为非阻塞
static int setNonBlocking(int fd){
    int oldOption = fcntl(fd, F_GETFL);
    int newOPtion = oldOption | O_NONBLOCK;
    fcntl(fd, F_SETFL, newOPtion);
    return oldOption;
}

// 绑定fd 到epoll事件表
static void addfd(int epollfd, int fd){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET // 边缘模式
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setNonBlocking(fd);
}

static void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 定义信号sig处理函数（把信号与I/O事件 一起交与epoll处理）统一信号处理
// 方式：（具体信号）通过管道 通知 epoll
static void sigHandler(int sig){
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char*) &msg, 1, 0);
    errno = save_errno;
}

// 添加信号事件 (信号， 信号处理函数， 可重入)
static void addsig(int sig, void(handler)(int), bool restart = true){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);    // 安装信号
}

/*
private:
    void setup_sig_pipe();
    void run_parent();
    void run_child();

    int m_process_number;   // 进程池中 进程总数
    int m_idx;              // 子进程在进程池中的 序号（0开始）
    int m_epollfd;          // epoll事件表
    int m_listenfd;         // 网络连接fd
    int m_stop;             // 子进程 是否停止运行
    process* m_sub_process; // 保存所有 子进程描述信息（是一个数组 new process[]） 
public:
    void run();
*/
// 统一事件源
template<typename T>
void processPool<T>::setup_sig_pipe(){
    // 同时最大5个epoll 信号事件
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);
    // 信号 通过 管道通信
    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    assert(ret != -1);
    setNonBlocking(sig_pipefd[1]);      // 非阻塞
    addfd(m_epollfd, sig_pipefd[0]);    // 设置读就绪

    // 安装信号
    addsig(SIGCHLD， sigHandler);
    addsig(SIGTERM, sigHandler);
    addsig(SIGINT, sigHandler);
    addsig(SIGPIPE, SIG_IGN);   // 忽略
}

// 执行过程
template<typename T>
void processPool<T>::run(){
    if (m_idx != -1){
        run_child();
        return;
    }
    run_parent();
}

template<typename T>
void processPool<T>::run_child(){
    // 统一事件源
    setup_sig_pipe();

    int pipefd = m_sub_process[m_idx].m_pipefd[1];  // 读fd
    addfd(m_epollfd, pipefd);       //添加到事件表中

    epoll_event events[MAX_EVENT_NUMBER];
    T* users = new T[USER_PER_PROCESS];
    assert(users);
    int number = 0, ret = -1;
    
    while (! m_stop)
    {
        number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if((number < 0) && (errno != EINTR)){
            printf("epoll failure\n");
            break;
        }
        for (int i = 0; i < number; i++)
        {
            
            int sockfd = events[i].data.fd;
            // 读就绪事件
            if((sockfd == pipefd) && (events[i].events & EPOLLIN)){
                
            }
            // 信号事件
            // 客户初始连接
            // 其他事件
        }
        
    }
    

}

#endif