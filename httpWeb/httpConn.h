#include <sys/epoll.h>
// #include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <errno.h>
#include <assert.h>
#include <stdarg.h>

#include </var/tmp/git_re/practice/web/httpWeb/locker.h>

// 定义连接类， 用于线程池模板的参数
class httpConn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFR_SIZE = 2048;
    static const int WRITE_BUFFR_SIZE = 1024;
    // 请求方式
    enum METHOD {GET = 0, POST, PUT, HAED};
    // 解析状态（请求行、头、内容）
    enum PARSE_STATE {PARSE_REQUESTLINE, PARSE_HEAD, PARSE_CONTENT};
    // 读取行状态（读到一行，不是行，未读完）
    enum LINE_STATE {LINE_OK = 0, LINE_BAD, LINE_OPEN};
    // HTTP状态码(200、301、403、404、503) INTERNAL_ERROR(process_read 500), FILE_REQUEST(200)
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDEN, FILE_REQUEST, INTNET_ERROR, INTERNAL_ERROR, CLOSE_CONNECTION};
    
    static int m_epollfd;
    static int m_user_count;

    // httpConn();
    // ~httpConn();
    void init(int sockfd, const sockaddr_in &addr);
    void closeConn(bool real_close = true);
    void process();
    bool read();    // 读入请求
    bool write();   // 写入响应

private:
    int m_sockfd;
    sockaddr_in m_address;
    // 读入参数
    char m_read_buf[READ_BUFFR_SIZE];
    int m_read_idx;         // 当前读位置
    int m_checked_idx;      // 下一个要读入位置
    int m_start_line;       // 当前处于第几个完整行
    // 写入参数
    char m_write_buf[WRITE_BUFFR_SIZE];
    int m_write_idx;

    PARSE_STATE m_parse_state;
    METHOD m_method;
    // 解析结果
    char m_real_file[FILENAME_LEN];
    char* m_url;
    char* m_version;
    char* m_host;
    int m_content_length;
    bool m_linger;           // http请求是否要求 保持连接
    // 准备响应
    char* m_file_path;
    struct stat m_file_stat;
    struct iovec m_iv[2];    // IO向量（存储多个buf）
    int m_iv_count;

    void init();
    // 处理读 // GET http://baidu.com/index.html HTTP/1.1 
    /*
    GET / HTTP/1.1
    Host: 127.0.0.1:19801
    User-Agent: Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:69.0) Gecko/20100101 Firefox/69.0
    Accept: text/html,application/xhtml+xml,application/xml;q=0.9,;q=0.8
    Accept-Language: en-US,en;q=0.5
    Accept-Encoding: gzip, deflate
    Connection: keep-alive
    Upgrade-Insecure-Requests: 1
    Cache-Control: max-age=0
    */
    // 处理读，解析函数
    HTTP_CODE process_read();
    char* get_line(){return m_read_buf + m_start_line;}
    LINE_STATE parse_line();
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    

    // 处理写，响应请求
    bool process_write(HTTP_CODE ret);
    void unmap();
    
    bool add_status_line(int status, const char* title);
    bool add_header(int contentLen);
    bool add_response(const char* format, ...);

    bool add_content(const char* content);
    bool add_content_length(int content_length);

    bool add_linger();
    bool add_blank_line();
};

