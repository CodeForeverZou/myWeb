#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <arpa/inet.h>
#include <errno.h>

#define USER_LIMIT 5
#define FD_LIMIT 65535  // fd限制
#define BUFF_SIZE 64

// 允许多个客户端接入
struct client_data
{
    sockaddr_in  addr;
    char* writeBuff;
    char buff[BUFF_SIZE];
};

// 设置fd为非阻塞的
int setNonBlocking(int fd){
    int oldOption = fcntl(fd, F_GETFL);
    int newOption = oldOption | O_NONBLOCK; //文件描述符为非阻塞
    fcntl(fd, F_SETFL, newOption);
    return oldOption;
}

int main(){
    // 服务器： socket bind listen  accept  poll recv send
    const char* ip = "127.0.0.1";
    int port = 19801;

    int ret = 0;
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    // socket
    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(sockfd >= 0);

    // bind
    ret = bind(sockfd, (struct sockaddr*) &server_addr, sizeof(server_addr));
    assert(ret != -1);

    // listen
    ret = listen(sockfd, 5);
    assert(ret != -1);

    // 初始化各个客户端，以及对应的poll
    client_data* users = new client_data[FD_LIMIT];
    pollfd fds[USER_LIMIT + 1];
    int user_cnt = 0;
    for (int i = 1; i <= USER_LIMIT; i++){
        fds[i].fd = -1;
        fds[i].events = 0;
    }
    // 第一个pollfd 完成网络连接
    fds[0].fd = sockfd;
    fds[0].events = POLLIN | POLLERR;
    fds[0].revents = 0;

    while (1)
    {
        ret = poll(fds, user_cnt + 1, -1);  // poll等待所言已存在的pollfd
        if (ret < 0){
            printf("poll failure\n");
            break;
        }

        // 依次处理每个pollfd
        for (int i = 0; i < user_cnt + 1; i++){
            // 若是 网络连接 事件
            if ((fds[i].fd == sockfd) && (fds[i].revents & POLLIN)){
                // 建立client， 并accept
                struct sockaddr_in client_addr;
                socklen_t client_data_len = sizeof(client_addr);
                // accept
                int connfd = accept(sockfd, (struct sockaddr*) &client_addr, &client_data_len);
                if (connfd < 0){
                    printf("conn error is %d\n", errno);
                    continue;
                }
                if (user_cnt >= USER_LIMIT){    // 连接过多
                    const char* info = "too many users";
                    printf("%s\n", info);
                    send(sockfd, info, strlen(info), 0);
                    close(connfd);
                    continue;
                }
                // 生成一个client，由文件描述符标记， 计数
                user_cnt++;
                users[connfd].addr = client_addr;
                setNonBlocking(connfd);     // 非阻塞的connfd
                fds[user_cnt].fd = connfd;
                fds[user_cnt].events = POLLIN | POLLRDHUP | POLLERR;
                fds[user_cnt].revents = 0;
                printf("\ncomes the %dst new user, his fd is %d\n", user_cnt, connfd);
            }
            // 事件错误
            else if (fds[i].revents & POLLERR){
                printf("get an error from %d\n", fds[i].fd);
                char errors[100];
                memset(errors, '\0', 100);
                socklen_t err_len = sizeof(errors);
                // 调用getsockopt 来获取sockfd错误
                if (getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, &errors, &err_len)){
                    printf("get socket option failed\n");
                }
                continue;
            }
            // 挂起一个client
            else if (fds[i].revents & POLLRDHUP){
                // 如果客户端关闭连接，则服务器也关闭对应连接
                users[fds[i].fd] = users[fds[user_cnt].fd];
                close(fds[i].fd);   // 关闭连接
                fds[i--] = fds[user_cnt];
                user_cnt--;
                printf("a client left\n");
            }
            // 接受输入
            else if (fds[i].revents & POLLIN){
                int connfd = fds[i].fd;
                memset(users[connfd].buff, '\0', BUFF_SIZE);
                // recv  为什么BUFFSIZE-1 ？？？
                ret = recv(connfd, users[connfd].buff, BUFF_SIZE - 1, 0);
                printf("get %d bytes from %d and client data is: %s ", ret, connfd, users[connfd].buff);
                if (ret < 0){
                    if (errno != EAGAIN){
                        close(connfd);
                        users[fds[i].fd] = users[fds[user_cnt].fd];
                        fds[i--] = fds[user_cnt];
                        user_cnt--;
                    }
                }
                else if (ret == 0){
                    printf("code shold not come to here\n");
                }
                // 如果接受到数据，则把该客户数据发送给每一个登录到此服务器上的客户端（数据发送者除外） ！！！
                else{
                    // printf("user_cnt:%d ", user_cnt);
                    for (int j = 1; j <= user_cnt; j++){
                        if (fds[j].fd == connfd) continue;  // 排除自身
                        fds[j].events |= ~POLLIN;
                        fds[j].events |= POLLOUT;   // 置为输出事件
                        printf("now else should printout!!\n");
                        users[fds[j].fd].writeBuff = users[connfd].buff;
                    }
                }
            }
            // 输出事件
            else if(fds[i].revents & POLLOUT){
                int connfd = fds[i].fd;
                if (!users[connfd].writeBuff) continue;
                // send
                ret = send(connfd, users[connfd].writeBuff, strlen(users[connfd].writeBuff), 0);
                printf("already send!\n");
                users[connfd].writeBuff = NULL;
                // 写完数据后，重新注册fds[i]的可读事件！
                fds[i].events |= ~POLLOUT;
                fds[i].events |= POLLIN;
            }
        }// for 每个user
    }// while(1)

    delete [] users;
    close(sockfd);
    return 0;
}