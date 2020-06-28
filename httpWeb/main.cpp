#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include </var/tmp/git_re/practice/web/httpWeb/httpConn.h>
#include </var/tmp/git_re/practice/web/httpWeb/threadPool.h>
#include </var/tmp/git_re/practice/web/httpWeb/locker.h>

#define MAX_FD 65536                // 宏定义最后不要加；
#define MAX_EVENT_NUMBER 10000

// 调用 httpConn.cpp 的fd操作
extern int addfd(int epollfd, int fd, bool oneShot);
extern int removefd(int epollfd, int fd);

// 信号操作
void addSig(int sig, void(handler)(int), bool restart = true){
    // 创建信号结构体
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

int main(){
    const char* ip ="127.0.0.1";
    const int port = 19801;

    addSig(SIGPIPE, SIG_IGN);
    // 创建线程池pool
    threadPool<httpConn>* pool = NULL;
    try{
        pool = new threadPool<httpConn>;
    }
    catch(...){
        return 1;
    }
    // 创建连接数组，记录连接用户，备用
    httpConn* users = new httpConn[MAX_FD];
    assert(users);
    int userCount = 0;

    // 建立socket、监听
    sockaddr_in serveAddr;
    serveAddr.sin_family = AF_INET;
    serveAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serveAddr.sin_addr);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    int ret = 0;
    ret = bind(listenfd, (struct sockaddr*) &serveAddr, sizeof(serveAddr));
    assert(ret >= 0);

    ret = listen(listenfd, 5);
    assert(ret >= 0);

    // 建立epoll内核事件表， 事件集events
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false);
    httpConn::m_epollfd = epollfd;

    while (true)
    {   
        // 循环监听 epoll 就绪事件， number个
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if((number < 0) && (errno != EINTR)){
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            // 处理 连接事件
            if(sockfd == listenfd){
                struct sockaddr_in clientAddr;
                socklen_t clientAddrLen = sizeof(clientAddr);
                int connfd = accept(sockfd, (struct sockaddr*) &clientAddr, &clientAddrLen);
                if(connfd < 0){
                    printf("errno is: %d\n", errno);
                    continue;
                }
                if(httpConn::m_user_count >= MAX_FD){
                    const char* info = "Internet server busy";
                    printf("%s\n", info);
                    send(connfd, info, strlen(info), 0);
                    close(connfd);
                }
                // 成功，存储客户连接
                users[connfd].init(connfd, clientAddr);
            }
            // 关闭事件
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
                users[sockfd].closeConn();
            // 读就绪事件
            else if(events[i].events & EPOLLIN){
                if(users[sockfd].read())
                    // 线程池 添加客户地址 即 &uers[sockfd],  V工作队列某个线程，执行process，完成读（解析请求）、写（HTTP应答，标记EPOLLOUT事件）
                    pool->append(users + sockfd);
            }
            // 写就绪事件
            else if(events[i].events & EPOLLOUT){
                if(! users[sockfd].write())
                    users[sockfd].closeConn();
            }
            else
            {}
        }
    }

    close(epollfd); 
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}