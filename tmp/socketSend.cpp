#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include <string.h>
#include <fcntl.h>

int main(int argc, char* argv[]){
    if (argc <= 2){
        printf("need port\n", basename(argv[0]));
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi(argv[2]);

    struct sockaddr_in server_address;
    bzero(&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_address.sin_addr);

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    assert( sockfd >= 0 );
    
    if (connect(sockfd, (struct sockaddr*) &server_address, sizeof(server_address)) < 0){
        printf("connect failed\n");
    }else
    {
        const char* cdata = "abcd";
        send(sockfd, cdata, strlen(cdata), 0);
        char text[] = "     ";
        recv(sockfd, text, strlen(text), 0);
        // printf(text);
    }
    close(sockfd);
    return 0;
    



}