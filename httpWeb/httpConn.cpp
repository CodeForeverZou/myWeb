#include </var/tmp/git_re/practice/web/httpWeb/httpConn.h>

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

    m_read_idx = 0;         // 当前读位置
    m_checked_idx = 0;      // 下一个要读入位置
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
// 主函数调用
bool httpConn::write(){

}

void httpConn::process(){

}

    /* 私有
    // 处理写，响应请求
    bool process_write(HTTP_CODE ret);
    void unmap();
    
    bool add_status_line(int status, const char* title);
    bool add_header(int contentLen);
    bool add_response(const char* format, ...)
    bool add_content(const char* content);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line(); 

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
                else if(ret == GET_REQUEST) // 继续解析头
                    return do_request();
                break;
            }
            case PARSE_CONTENT:{
                ret = parse_content(text);
                if (ret == GET_REQUEST)
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
// httpConn::httpConn()
// {
// }

// httpConn::~httpConn()
// {
// }