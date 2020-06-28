#include </var/tmp/git_re/practice/web/httpWeb/httpConn.h>

// 网站根目录
const char* doc_root = "/var/tmp/git_re/practice/";

// 设置fd非阻塞
int setNonBlock(int fd){
    int oldOption = fcntl(fd, F_GETFL);
    int newOption = oldOption | O_NONBLOCK;
    fcntl(fd, F_SETFL, newOption);
    return oldOption;
}

// fd注册到epoll事件表
void addfd(int epollfd, int fd, bool oneShot){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if(oneShot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setNonBlock(fd);
}

void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0 );
    close(fd);
}

void modfd(int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// --------------------------http连接-------------------------------
int httpConn::m_user_count = 0;
int httpConn::m_epollfd = -1;

/*  公有函数        客户有四种需求：建立连接（init）、HTTP请求（read 注：请求到来赶紧添加到工作队列，交付线程处理)、HTTP响应（write 注：是执行完HTTP请求分析即read后，马上进行的）、关闭连接
    void init(int sockfd, const sockaddr_in &addr);
    void closeConn(bool real_close = true);
    void read();    // 读入请求
    void write();   // 写入响应
    void process(); // 由线程池模板调用的（由线程完成具体任务） 线程是通过（工作队列）PV信号量来知道有木有工作*/
void httpConn::closeConn(bool real_close){
    if(real_close && (m_sockfd != -1)){
        removefd(m_epollfd, m_sockfd);  // 断开连接
        m_sockfd = -1;
        m_user_count--;
    }
}
// 公有初始化
void httpConn::init(int sockfd, const sockaddr_in& clientAddr){
    m_sockfd = sockfd;
    m_address = clientAddr;
    // int error = 0;
    // socklen_t len = sizeof(error);
    // getsockopt(m_sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
    // 为了避免TIME_WAIT状态，仅用在调试时
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET,SO_REUSEADDR, &reuse, sizeof(reuse));
    // 注册到epoll事件表
    addfd(m_epollfd, sockfd, true);     
    m_user_count++;
    
    init();
}
// 私有初始化
void httpConn::init(){
    m_parse_state = PARSE_REQUESTLINE;
    
    m_method = GET;
    m_url = 0;      // 0 = '\0' = NULL = nullptr
    m_version = 0;
    m_host = 0;
    m_content_length = 0;
    m_linger = false;

    m_read_idx = 0;         // 下一个要读入位置当前读位置
    m_checked_idx = 0;      // 当前读到位置
    m_start_line = 0;       // 当前处于第几个完整行
    m_write_idx = 0;
    
    memset(m_read_buf, '\0', READ_BUFFR_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFR_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}
// 主函数执行
bool httpConn::read(){
    if(m_read_idx >= READ_BUFFR_SIZE)
        return false;
    int bytesRead = 0;
    // 循环读，直到无数据，或对方断开
    while (true)
    {
        bytesRead = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFR_SIZE - m_read_idx, 0);
        if(bytesRead == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return false;
        }
        else if(bytesRead == 0){
            return false;
        }
        m_read_idx += bytesRead;
    }
    return true;
}

// 释放内存映射区
void httpConn::unmap(){
    if(m_file_path){
        munmap(m_file_path, m_file_stat.st_size);
        m_file_path = 0;
    }
}

// 主函数调用
bool httpConn::write(){
    int temp = 0;
    int bytesHasSend = 0;
    int bytesToSend = m_write_idx;
    if(bytesToSend == 0){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= -1){
            if(errno == EAGAIN){
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();    // 清空缓存
            return false;
        }

        bytesToSend -= temp;
        bytesHasSend += temp;
        if(bytesToSend <= bytesHasSend){
            unmap();
            if(m_linger){
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            }
            else{
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

// 当得到了一个正确的HTTP请求，就分析目标文件属性，（看请求资源是否存在、是否所有用户可读、且不是目录）
// 然后使用mmap将其映射到内存地在m_file_path处， 并通知获取文件成功。
httpConn::HTTP_CODE httpConn::do_request(){
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    // 把url请求文件 + 在网站根目录后
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len -1);
    // stat() 获取文件属性
    if(stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    if(! (m_file_stat.st_mode & S_IROTH))
        return FORBIDEN;
    if(S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    // 成功获取到
    int filefd = open(m_real_file, O_RDONLY);
    m_file_path = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, filefd, 0);
    close(filefd);
    return FILE_REQUEST;
}

/* 私有
注：具体数据已有read全部读入m_read_buf中   process   process_read   parse_line\ parse_request_line/headers/content
// 处理读，解析函数         过程：线程调用process，再依次读写调用process_read； 一行一行读parse_line 分析请求行、头、内容 
HTTP_CODE process_read();
char* get_line(){return m_read_buf + m_start_line;}*/
httpConn::LINE_STATE parse_line();
httpConn::HTTP_CODE parse_request_line(char* text);
httpConn::HTTP_CODE parse_headers(char* text);
httpConn::HTTP_CODE parse_content(char* text);
httpConn::HTTP_CODE do_request();

// 主状态机：负责分发各读取状态下 对应任务
httpConn::HTTP_CODE httpConn::process_read(){
    LINE_STATE line_state = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    // 循环解析，获得每一行
    while (((m_parse_state == PARSE_CONTENT) && (line_state == LINE_OK))
            || ((line_state = parse_line()) == LINE_OK))
    {
        text = get_line();  // char* get_line(){return m_read_buf + m_start_line;}
        m_start_line = m_checked_idx;
        printf("got one http line: %s\n",text);
        // 根据全局变量 m_parse_state 决定解析到哪一步
        switch (m_parse_state)
        {
            case PARSE_REQUESTLINE:{
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                break;
            }
            case PARSE_HEAD:{
                ret = parse_headers(text);
                if(ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if(ret == GET_REQUEST) // 得到了 正确的HTTP请求
                    return do_request();
                break;
            }
            case PARSE_CONTENT:{
                ret = parse_content(text);
                if (ret == GET_REQUEST)     // 得到了 正确的HTTP请求
                    return do_request();
                line_state = LINE_OPEN;     // 表示未读完
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}
// 从状态机：具体解析出每一行
httpConn::LINE_STATE httpConn::parse_line(){
    char temp;
    for(;m_checked_idx < m_read_idx; m_checked_idx++){
        temp = m_read_buf[m_checked_idx];
        // 检测\r\n 得到一行 （每个头部字段、请求行、空行都遵循<CR><LF>回车换行符
        if(temp == '\r'){
            if((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if(m_read_buf[m_checked_idx + 1] == '\n'){
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n'){
            if((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')){
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

httpConn::HTTP_CODE httpConn::parse_request_line(char* text){
    m_url = strpbrk(text, " \t");
    if(!m_url) return BAD_REQUEST;
    *m_url++ = '\0';

    char* method = text;
    if(strcasecmp(method, "GET") == 0)
        m_method = GET;
    else
        return BAD_REQUEST;     // 只处理get
    
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if(!m_version) return BAD_REQUEST;
    *m_version++ = '\0';
    if(strcasecmp(m_version, "HTTP/1.1") != 0) 
        return BAD_REQUEST;     // 只接受HTTP/1.1
    
    if(strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if(!m_url || m_url[0] != '/') // 只处理'/'
        return BAD_REQUEST; 

    printf("The request URL is:%s\n", m_url);
    m_parse_state = PARSE_HEAD; // 请求行处理完成， 改为处理头部字段
    return NO_REQUEST;
}

httpConn::HTTP_CODE httpConn::parse_headers(char* text){
    if(text[0] == '\0'){
        // if (m_method == HEAD) return GET_REQUEST;
        if(m_content_length != 0){
            m_parse_state = PARSE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0){
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0)
            m_linger = true;
    }
    else if(strncasecmp(text, "Content-Length:", 15) == 0){
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else{
        printf("oop! I can't parse header %s\n", text);
    }
    return NO_REQUEST;
}

httpConn::HTTP_CODE httpConn::parse_content(char* text){
    if(m_read_idx >= (m_content_length + m_checked_idx)){
        text[m_content_length] = '\0';
        printf("parse content len %d\n", m_content_length);
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 处理写，响应请求
/*
bool httpConn::add_status_line(int status, const char* title);
bool httpConn::add_header(int contentLen);
bool httpConn::add_response(const char* format, ...);
bool httpConn::add_content(const char* content);
bool httpConn::add_content_length(int content_length);
bool httpConn::add_linger();
bool httpConn::add_blank_line();
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";

const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";

const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";

const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
const char* doc_root = "/var/www/html";
*/

bool httpConn::process_write(httpConn::HTTP_CODE ret){
    switch (ret)
    {
        case INTERNAL_ERROR:{ // 服务器错误
            add_status_line(500, "Internal Error");
            add_header(strlen("There was an unusual problem serving the requested file.\n"));
            if(! add_content("There was an unusual problem serving the requested file.\n")) return false;
            break;
        }
        case BAD_REQUEST:{
            add_status_line(400, "Bad Request");
            add_header(strlen("The requested file was not found on this server.\n"));
            if(! add_content("The requested file was not found on this server.\n")) return false;
            break;
        }
        case NO_RESOURCE:{
            add_status_line(404, "Not Found");
            add_header(strlen("The requested file was not found on this server.\n"));
            if(! add_content("The requested file was not found on this server.\n")) return false;
            break;
        }
        case FORBIDEN:{
            add_status_line(403, "Forbidden");
            add_header(strlen("The requested file was not found on this server.\n"));
            if(! add_content("The requested file was not found on this server.\n")) return false;
            break;
        }
        case FILE_REQUEST:{
            add_status_line(200, "OK");
            if(m_file_stat.st_size != 0){
                add_header(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;                                                                      ;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_path;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else{
                add_header(strlen("<html><body>nothing for give</body></html>"));
                if(! add_content("<html><body>nothing for give</body></html>")) return false;
            }
        }
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

 bool httpConn::add_response(const char* format, ...){
    if(m_write_idx >= WRITE_BUFFR_SIZE) return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFR_SIZE -1 - m_write_idx,
                    format, arg_list);
    if(len >= (WRITE_BUFFR_SIZE -1 - m_write_idx)) return false;
    m_write_idx += len;
    va_end(arg_list);
    return true;
 }

 bool httpConn::add_status_line(int status, const char* title){
     return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
 }

 bool httpConn::add_header(int len){
     // len linger blankline
    add_response("Content-Length: %d\r\n", len);
    add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
    add_response("%s", "\r\n");
 }

 bool httpConn::add_content(const char* content){
     return add_response("%s", content);
 }

// 线程调用！！！ 完成读写
void httpConn::process(){
    // 读
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    // 写
    bool write_ret = process_write(read_ret);
    if (!write_ret) closeConn();

    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

// httpConn::httpConn()
// {
// }

// httpConn::~httpConn()
// {
// }