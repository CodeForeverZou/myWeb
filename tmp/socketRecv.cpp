#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

int main(int argc, char* argv[]){
    if (argc <= 2){
        printf("need port\n", basename(argv[0]));
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi(argv[2]);

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);

    int ret = bind(sock, (struct sockaddr*) &address, sizeof(address));
    assert(ret != -1);

    // sleep(20);  // 等待连接
    ret = listen(sock, 5);
    assert(ret != -1);

    struct sockaddr_in client;
    socklen_t client_addrlenth = sizeof(client);
    while (1)
    {
        int connfd = accept(sock, (struct sockaddr*) &client, &client_addrlenth);
        if(connfd < 0){
            // printf("errno is: %d\n", errno);
            continue;
        }else
        {
            printf("success\n");
            int pipefd[2];
            assert(ret != -1);  //防止中断
            ret = pipe(pipefd);
            ret = splice(connfd, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
            assert( ret != -1 );
            // char text[] = "           ";
            // recv(sock, text, strlen(text), 0);

            close(connfd);
        }

    }
    
    
    close(sock);
    return 0;
    

}
