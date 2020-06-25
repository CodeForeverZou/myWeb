#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>

#define MAX_EVENT_MUMBER 1024
static int pipefd[2];   // 共用管道

// 设置文件描述符 非阻塞的
int setNonBlocking(int fd){
    int oldOption = fcntl(fd, F_GETFL);
    int newOption = oldOption | O_NONBLOCK;
    fcntl(fd, F_SETFL, newOption);
    return oldOption;
}

// 将fd上的EPOLLIN注册到  epollfd指定的epoll内核事件表中
void addFd(int epollfd,int fd){         // 图快，把fd 写在 epollfd前，调用时却忘记改过位置！ 导致无显示
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;   // 使用ET模式（边缘触发）
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setNonBlocking(fd);
}

// 信号处理函数
void sigHandler(int sig){       // 倒也不是**函数名不能大写，否则出错！！！** 好像运行第二遍就没问题列
    // 保留原来的errno，在函数最后恢复，保证函数的可重入性
    int saveErrno = errno;
    int msg = sig;
    // 将信号写入管道， 通知主循环 ！！！
    send(pipefd[1], (char*) &msg, 1, 0);
    errno = saveErrno;
}

void addSig(int sig){
    struct sigaction sa;        // sigaction结构体描述 信号处理细节
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = sigHandler;
    sa.sa_flags |= SA_RESTART;  // 设置接收到信号的行为， SA_RSTART重新调用 被其终止的系统调用
    sigfillset(&sa.sa_mask);    // 在信号集中设置所有信号
    assert( sigaction(sig, &sa, NULL) != -1);   // 信号安装函数 ！！！ sigaction
}
// void addSig( int sig )
// {
//     struct sigaction sa;
//     memset( &sa, '\0', sizeof( sa ) );
//     sa.sa_handler = sighandler;
//     sa.sa_flags |= SA_RESTART;
//     sigfillset( &sa.sa_mask );
//     assert( sigaction( sig, &sa, NULL ) != -1 );
// }

int main(){
    // 创建socket 监听（socket、bind、listen、accept、send）
    const char* ip = "127.0.0.1";
    int port = 19801;

    int ret = 0;
    struct sockaddr_in serverAddr;
    bzero(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serverAddr.sin_addr);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    ret = bind(listenfd, (struct sockaddr*) &serverAddr, sizeof(serverAddr));
    if (ret == -1){
        printf("errno is %d\n", errno);
        return 1;
    }
    ret = listen(listenfd, 5);
    assert(ret != -1);

    // epoll_event结构体： 包括事件、用户数据
    epoll_event events[MAX_EVENT_MUMBER];
    int epollfd = epoll_create(5);      // 创建epollfd描述符
    assert(epollfd != -1);
    addFd(epollfd, listenfd);   // 添加到epollfd事件表中

    // 使用socketpair创建管道， 注册pipefd[0]上的可读事件
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setNonBlocking(pipefd[1]);
    addFd(epollfd, pipefd[0]);
    
    // 设置一些要处理的信号
    addSig(SIGHUP);
    addSig(SIGCHLD);
    addSig(SIGTERM);
    addSig(SIGINT);     // 中断
    bool stop_server = false;

    while (!stop_server)
    {
        // epoll_wait 查询I/O事件 数
        int number = epoll_wait(epollfd, events, MAX_EVENT_MUMBER, -1);
        if ((number < 0) && (errno != EINTR)){
            printf("epoll failure\n");
            break;
        }

        // 处理就绪的fd
        for (int i = 0; i < number; i++){
            int sockfd = events[i].data.fd;
            // 若是socket连接
            // printf("%d\n",i);
            if (sockfd == listenfd){
                // 接受
                struct sockaddr_in clientAddr;
                socklen_t clientAddr_len = sizeof(clientAddr);
                int connfd = accept(listenfd, (struct sockaddr*) &clientAddr, &clientAddr_len);
                printf("somebody connect, num is %d\n", connfd);
                addFd(epollfd, connfd);     // 添加到 epollfd事件表中
            }
            else if ( (sockfd == pipefd[0]) && (events[i].events & EPOLLIN)){
                printf("get sig, total num is %d\n", number);
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if(ret == -1)
                    continue;
                else if(ret == 0)
                    continue;
                else{
                    for(int i = 0; i < ret; i++){
                        // 根据信号类型，处理信号
                        switch (signals[i])
                        {
                            case SIGCHLD:
                                // printf("sig is HLD\n");
                            case SIGHUP:
                                // printf("sig is HUP\n");
                                continue;
                            case SIGTERM:
                                // printf("sig is SIGTERM\n");
                            case SIGINT:
                                stop_server = true;
                        }
                    }
                }// if recv()
            }
            else{}// if sockfd == 
        }//for 就绪事件
    }
    
    printf("close fds\n");
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    return 0;
}