#include </var/tmp/git_re/practice/web/cgiWeb/processPool.h>

// 用于处理客户cgi请求的类， 将它作为processPool类的模板参数 
class cgiConn
{
private:
    // 读缓冲区大小
    static const int BUFF_SIZE = 1024;
    static int m_epollfd;
    int m_sockfd;
    sockaddr_in m_address;
    char m_buf[BUFF_SIZE];
    // 标记缓冲区中 已读入数据 的最后字符的下个位置
    int m_read_idx;
public:
    // cgiConn();
    // ~cgiConn();
    // 初始化函数（初始化客户端连接，清空缓冲区）
    void init(int epollfd, int sockfd, const sockaddr_in &clientAddr){
        m_epollfd = epollfd;
        m_sockfd = sockfd;
        m_address = clientAddr;
        memset(m_buf, '\0', BUFF_SIZE);
        m_read_idx = 0;
    }

    // 定义子进程处理函数
    void process(){
        int idx = 0;
        int ret = -1;
        // 循环读取、分析客户数据
        while (true)
        {
            idx = m_read_idx;   // 当前要读入位置idx
            ret = recv(m_sockfd, m_buf + idx, BUFF_SIZE - 1, 0);
            // 若读入错误，关闭客户端连接；若无数据可读，退出循环
            if(ret < 0){
                if (errno != EAGAIN)
                    removefd(m_epollfd, m_sockfd);
                break;
            }
            // 若对方关闭连接，则服务器也断开
            else if(ret == 0){
                removefd(m_epollfd, m_sockfd);
                break;
            }
            // 有数据
            else{
                m_read_idx += ret;  // 下一次要读入的位置
                printf("user content is: %s\n", m_buf);
                // 若遇到“\r\n"，开始处理客户请求
                for(; idx < m_read_idx; idx++){
                    if((idx >= 1) && (m_buf[idx-1] == '\r') && (m_buf[idx] == '\n'))
                        break;
                }
                // 若没有遇到，则需处理更多的数据
                if(idx == m_read_idx) continue;

                // 否则，分析请求
                m_buf[idx - 1] = '\0';
                char* file_name = m_buf;
                // 判断 客户需要运行的 cgi程序 是否存在？
                if (access(file_name, F_OK) == -1){
                    removefd(m_epollfd, m_sockfd);
                    break;
                }
                // 存在，则创建子进程执行cgi程序
                ret = fork();
                if(ret == -1){
                    removefd(m_epollfd, m_sockfd);
                    break;
                }
                else if(ret > 0){
                    // 父进程只需关闭连接
                    removefd(m_epollfd, m_sockfd);
                    break;
                }
                else{
                    // 子进程将标准输出 定向到 m_sockfd, 并执行cgi程序
                    close(STDOUT_FILENO);
                    dup(m_sockfd);
                    execl(m_buf, m_buf, 0);
                    exit(0);
                }
            }
        }
    }
};

int cgiConn::m_epollfd = -1;

int main(){
    const char* ip = "127.0.0.1";
    int port = 19801;
    
    // 建立服务器，监听
    int ret = 0;
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serverAddr.sin_addr);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert( listenfd != -1 );

    ret = bind(listenfd, (struct sockaddr*) &serverAddr, sizeof(serverAddr));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);
    
    // 调用进程池模板 完成主函数  
    processPool<cgiConn>* pool = processPool<cgiConn>::create(listenfd);
    if (pool){
        // printf("has pool\n");
        pool->run();
        delete pool;
    }
    // printf("no pool\n");
    // 这里关闭listen（主函数创建，由主函数关闭）
    close(listenfd);
    return 0;
}