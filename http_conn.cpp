#include "http_conn.h"
#include "http_respon_info.h"


int setnonblocking( int fd)
{
    int old_opt = fcntl(fd, F_GETFL);
    int new_pot = old_opt |O_NONBLOCK;
    fcntl(fd,F_SETFL,new_pot);
    return old_opt;
}

void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN| EPOLLET| EPOLLRDHUP;
    if(one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd, &event);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,NULL);
    close(fd);
}

void modfd(int epollfd, int fd, int ev_op)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev_op | EPOLLET| EPOLLONESHOT| EPOLLRDHUP;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn(bool real_close)
{
    if(real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd,m_sockfd);
        m_sockfd = -1;
        m_user_count --;
    }
}

void http_conn::init( int sockfd, const sockaddr_in & addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    
    // to aovid TIME_WAIT, only for debug, remove in real establish
    int reuse = 1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR, & reuse, sizeof(reuse));

    addfd( m_epollfd, sockfd, true);
    m_user_count++;
    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf,'\0',READ_BUFFER_SIZE);
    memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
    memset(m_real_file,'\0',FILENAME_LEN);
}


// slave state machine
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for(; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        if( temp == '\r')
        {
            if((m_checked_idx+1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n')
        {
            if((m_checked_idx >1) && (m_read_buf[m_checked_idx-1] == '\r'))
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;  
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}