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

#define BUFF_SIZE 64

int main(int argc, char* argv[]){
    // 客户端：clientSocket server_addr connect send recv poll
    // if (argc <= 2){
    //     printf("need port\n");
    //     return 1;
    // }
    const char *ip = "127.0.0.1"; // argv[1];
    int port = atoi("19801"); // argv[2]);

    // server_address
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    // clientSocket  connect
    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(sockfd >= 0);
    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0){
        printf("connect failed\n");
        close(sockfd);
        return 1;
    }

    // 建立poll，同时处理socket连接、标准输入读写    
    pollfd fds[2];
    fds[0].fd = 0;  // 0 表示标准输入
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    fds[1].fd = sockfd;
    fds[1].events = POLLIN | POLLRDHUP;
    fds[1].revents = 0;

    // pipe 建立管道
    char read_buf[BUFF_SIZE];
    int pipefd[2];
    assert( pipe(pipefd) != -1 );

    while (1)
    {
        // poll 事件分析 （两种事件）
        int ret = poll(fds, 2, -1);
        if(ret < 0){
            printf("poll failed\n");
            break;
        }

        if(fds[1].revents & POLLRDHUP){ // socket事件处理
            printf("server close the connection\n");
            break;
        }
        else if(fds[1].revents & POLLIN){
            memset(read_buf, '\0', BUFF_SIZE);
            recv(fds[1].fd, read_buf, BUFF_SIZE-1, 0);
            printf("msg is: %s\n", read_buf);
        }

        if(fds[0].revents & POLLIN){    // 输入事件处理
            // 从标准输入0 ， 通过管道传输给， sockfd 
            ret = splice(0, NULL, pipefd[1], NULL, 32768, 0);
            ret = splice(pipefd[0], NULL, sockfd, NULL, 32768, 0);
        }
    }

    close(sockfd);
    return 0;
}




// 测试 splice 管道 定向到标准输入输出
// int main(){
//     int pipedfd[2];
//     int ret = pipe(pipedfd);
//     ret = splice(0, NULL, pipedfd[1], NULL, 32768, SPLICE_F_MOVE | SPLICE_F_MORE);
//     assert( ret != -1 );
//     ret = splice(pipedfd[0], NULL, 0, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
//     assert(ret != -1);
    
//     return 0;
// }

