//编写http解析，请求行、头部
// 读状态、解析状态、行状态
// 完成后返回？

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#define BUFFER_SIZE 4096
enum PARSE_STATE {PARSE_REQUESTLINE, PARSE_HEAD, PARSE_CONTENT};
enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};
enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, FORBIDEN_REQUEST, CLOSE_CONNECT, INTERNAL_ERROR};


// 读取行
LINE_STATUS parse_line(char* buffer, int &checked_index, int &read_index){
    char temp;
    for ( ; checked_index < read_index; checked_index++)
    {
        temp = buffer[checked_index];
        if (temp == '\r'){
            if ((checked_index + 1) == read_index)  // buffer读完了， 总体未读完
                return LINE_OPEN;
            else if (buffer[checked_index+1] == '\n'){
                buffer[checked_index++] = '\0';
                buffer[checked_index++] = '\0';
                return LINE_OK;                     // 完整行
            }
            return LINE_BAD;
        }
        else if(temp == '\n'){
            if ((checked_index > 1) && buffer[checked_index - 1] == '\r'){  // 到头部了
                buffer[checked_index++] = '\0';
                buffer[checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;       // 未读完
    
}

// 解析请求行
HTTP_CODE parse_requestline(char* szTemp, PARSE_STATE &parseState){
    // GET http://baidu.com/index.html HTTP/1.1 

    char* url = strpbrk(szTemp, " \t"); // 查找空字符第一个位置,并分割
    if(!url) return BAD_REQUEST;
    *url++ = '\0';  // 分段

    char* method = szTemp;
    if (strcasecmp(method, "GET") == 0)
        printf("The request methord is GET\n");
    else
        return BAD_REQUEST;


    url += strspn(url, " \t");      // 寻找距空字符的距离
    char* version = strpbrk(url, " \t");
    if (!version) return BAD_REQUEST;
    *version++ = '\0';
    version += strspn(version, " \t");
    if (strcasecmp(version, "HTTP/1.1") != 0) return BAD_REQUEST;


    if (strncasecmp(url, "http://", 7) == 0){
        url += 7;
        url = strchr(url, '/');
    }
    if (!url || url[0] != '/') return BAD_REQUEST;
    
    printf("The request URL is:%s\n", url);
    parseState = PARSE_HEAD;
    return NO_REQUEST;
}

HTTP_CODE parse_headers(char* szTemp){
    if (szTemp[0] == '\0')
        return GET_REQUEST;
    else if (strncasecmp(szTemp, "Host:", 5) == 0){
        szTemp += 5;
        szTemp += strspn(szTemp, " \t");
        printf("the request host is:%s\n", szTemp);
    }else
        printf("I can't handle this header\n");
    
    return NO_REQUEST;
}

// 解析入口
HTTP_CODE parse_content(char* buffer, int &checked_index, int &read_index, int &start_line, PARSE_STATE &parseState){
    LINE_STATUS lineState = LINE_OK;
    HTTP_CODE retCode = NO_REQUEST;
    while ((lineState = parse_line(buffer, checked_index, read_index)) == LINE_OK)  // 读到一行
    {
        char* szTemp = buffer + start_line;
        start_line = checked_index;
        switch (parseState)
        {
        case PARSE_REQUESTLINE:       // 解析 请求行
            retCode = parse_requestline(szTemp, parseState);
            if (retCode == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        case PARSE_HEAD:              // 解析 头部
            retCode = parse_headers(szTemp);
            if (retCode == BAD_REQUEST)
                return BAD_REQUEST;
            else if(retCode == GET_REQUEST)
                return GET_REQUEST;
            break;

        default:                      // 错误
            return INTERNAL_ERROR;
            break;
        }
    }
    if (lineState == LINE_OPEN) // 还未读完
        return NO_REQUEST;
    else
        return BAD_REQUEST;
    
}
// HTTP应答
void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Server: myhttpd/0.1.0\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n<br>");
    send(client, buf, strlen(buf), 0);
    send(client, "<h1>I get your request", strlen("I get your request"), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

void acceptReq(int fd){
    char buffer[BUFFER_SIZE];
    memset(buffer, '\0', BUFFER_SIZE);
    int data_read = 0;  // 数据大小
    int read_index = 0; // 当前位置
    int checked_index = 0; // 读 目标位置
    int start_line = 0; // 当前行
    PARSE_STATE parseState = PARSE_REQUESTLINE;
    while (1)
    {
        data_read = recv(fd, buffer + read_index, BUFFER_SIZE - read_index, 0);
        if (data_read == -1){
            printf("reading failed.\n");
            break;
        }else if (data_read == 0){
            printf("remote client has closed connection\n");
            break;
        }

        read_index += data_read;
        HTTP_CODE result = parse_content(buffer, checked_index, read_index, start_line, parseState);
        if (result == NO_REQUEST){          // 未读完
            continue;
        }
        else if(result == GET_REQUEST){     // 完整读完
            printf("begin send info to client\n");
            not_found(fd);
            
            break;
        }
        else{                               // 出错
            send(fd, "parse request failed", strlen("parse request failed"), 0);
            break;
        }
    }
    close(fd);
}

int main(int argc, char* argv[]){
    // socket、bind、listen、accecpt
    // 获取ip、port
    if (argc <= 2){
        printf("need port\n");
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi(argv[2]);

    // 生成socket
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);
    
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    // bind
    int ret = bind(listenfd, (struct sockaddr*) &address, sizeof(address));
    assert(ret != -1);

    // listen
    ret = listen(listenfd, 5);
    assert(ret != -1);

    // 循环accept
    struct sockaddr_in client_address;
    socklen_t client_address_len = sizeof(client_address);
    while (1)
    {
        int fd = accept(listenfd, (struct sockaddr*) &client_address, &client_address_len);
        if (fd < 0){
            continue;
        }else
        {
            acceptReq(fd);
        }// if
    }// while

    close(listenfd);
    return 0;
}