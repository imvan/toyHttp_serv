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
    //int reuse = 1;
    //setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR, & reuse, sizeof(reuse));

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
    m_redis_request = 0;
    m_redis_response = 0;
    m_mysql_request = 0;
    m_mysql_response = 0;
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


bool http_conn::read()
{
    if(m_read_idx >= READ_BUFFER_SIZE)
    {
        printf("idx bigger than read buffer size\n");
        return false;
    }

    int bytes_read = 0;
    while(true)
    {
        bytes_read = recv(m_sockfd,m_read_buf+m_read_idx, 
                        READ_BUFFER_SIZE - m_read_idx,0);
        
        if(bytes_read == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            return false;
        }

        else if( bytes_read == 0)
        {
            printf("maybe the client closed\n");
            return false;
        }

        m_read_idx += bytes_read;

    }
    return true;
}

//to get the request method, URL ,and version
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{


    printf("in parse_request_line, we got text: %s\n",text);
    m_url = strpbrk(text," \t");
    printf("m_url is %s\n",m_url);
    if( !m_url )
    {
        printf("no url!\n");
        return BAD_REQUEST;
    }
    *m_url++ = '\0';



    //parse the requst method
    char* method = text;
    if(strcasecmp(method,"GET") == 0)
    {
        printf("we got a GET request\n");
        m_method = GET;
    }
    else if(strcasecmp(method,"POST") == 0)
    {
        printf("we got a POST request\n");
        m_method = POST;
    }
    else if(strcasecmp(method,"PUT") == 0)
    {
        printf("we got a POST PUT\n");
        m_method = PUT;
    }
    else if(strcasecmp(method,"DELETE") == 0)
    {
        printf("we got a DELETE request\n");
        m_method = DELETE;
    }
    else
    {
        printf("unknowing requst method\n");
        return BAD_REQUEST;
    }





    m_url += strspn( m_url, " \t");
    m_version = strpbrk( m_url, " \t");

    if(!m_version)
    {
        printf("no version!\n");
        return BAD_REQUEST;
    }

    *m_version++ = '\0';
    m_version += strspn( m_version, " \t");
    if ( strcasecmp( m_version, "HTTP/1.1") != 0)
    {
        printf("wrong HTTP version\n");
        return BAD_REQUEST;
    }

    if( strncasecmp( m_url, "http://", 7) == 0)
    {
        printf("NO http:// found in url\n");
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if( !m_url || m_url[0] != '/')
    {
        printf("wrong url\n");
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//parse the header
http_conn::HTTP_CODE http_conn::parse_headers( char* text)
{
    //when we got a empty line
    //finish the parse
    if( text[0] == '\0')
    {
        //if threre is request body
        if(m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        //otherwise, we got a complete HTTP request
        return GET_REQUEST;
    }

    //process Connection
    else if( strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, "\t");
        if(strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }

    //process Content-Length
    else if(strncasecmp(text, "Content-Length:",15) == 0)
    {
        text += 15;
        text += strspn(text,"\t");
        m_content_length = atol(text);
    }

    //process Host
    else if(strncasecmp(text, "Host:", 5) == 0)
    {
        text +=5;
        text += strspn(text,"\t");
        m_host = text;
    }

    else
    {
        printf("unknown header %s\n",text);
    }


    return NO_REQUEST;
    
}


//HTTP request body, we don't parse it
http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while( ( (m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK) ) 
        || ( ( line_status = parse_line() ) == LINE_OK ))
    {
        //get the text start positon in buffer;
        text = get_line();

        //point the start position in the end;
        m_start_line = m_checked_idx;
        printf("got 1 http line: %s\n", text);

        switch( m_check_state )
        {
            case CHECK_STATE_REQUESTLINE:
            {
                printf("in CHECK_STATE_REQUESTLINE\n");
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST)
                {
                    printf("its a bad request\n");
                    return BAD_REQUEST;
                }
                break;
            }

            case CHECK_STATE_HEADER:
            {
                printf("in CHECK_STATE_HEADER\n");
                ret = parse_headers(text);
                if(ret == BAD_REQUEST)
                {
                    printf("its a bad request\n");
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST)
                {
                    printf("its a correct request\n");
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                printf("in CHECK_STATE_CONTENT\n");
                ret = parse_content(text);
                if( ret == GET_REQUEST)
                {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }

    }

    return NO_REQUEST;
}





//when we got a correct complete HTTP request
//then analyse the target file

http_conn::HTTP_CODE http_conn::do_request()
{

    //parse api request here;
    // all url start with /api  
    if(strncasecmp(m_url,"/api",4) == 0)
    {
        
        printf("its an database request\n");

        m_url += strspn(m_url,"/api");

        return parse_api();

    }



    //process file request here:

    //the file path inited to the root path
    strcpy( m_real_file, doc_root);
    int len = strlen(doc_root);
    
    //add url to the tail of root path
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len -1);

    //get file attributs and put it to the buf
    if( stat(m_real_file, &m_file_stat) <0 )
    {
        printf("We got no this file\n");
        return NO_RESOURCE;
    }

    if ( ! (m_file_stat.st_mode & S_IROTH))
    {
        printf("This file cannot be access\n");
        return FORBIDDEN_REQUEST;
    }

    if( S_ISDIR( m_file_stat.st_mode))
    {
        printf("The url is a DIR, not a file name\n");
        return BAD_REQUEST;
    }


    //map file to the shared memory
    int fd = open( m_real_file, O_RDONLY);
    m_file_address = (char *)mmap( 0, m_file_stat.st_size, PROT_READ,
                       MAP_PRIVATE, fd, 0 );
    
    close(fd);
    return FILE_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_api()
{

    printf("in parse_api\n");

    if(strncasecmp(m_url,"redis",5) == 0)
    {
        printf("a redis request\n");
        m_redis_request = strpbrk(m_url,"?");
        do_redis_query();
        return do_redis_query();
;
    }
    else if (strncasecmp(m_url,"mysql",5) == 0)
    {
        printf("a mysql request\n");
        m_mysql_request = strpbrk(m_url,"?");
        return do_mysql_query();
;
    }
    else
    {
        printf("we dont support other api now\n");
        return BAD_REQUEST;
    }
    
    
}

http_conn::HTTP_CODE http_conn::do_redis_query()
{
    
}
http_conn::HTTP_CODE http_conn::do_mysql_query()
{

}



//unmap the shared memory
void http_conn::unmap()
{
   if(m_file_address)
   {
       munmap(m_file_address,m_file_stat.st_size);
       m_file_address = 0;
   } 
}


//write the HTTP response
bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;

    //if no data to send, re-init the connection
    printf("in write\n");
    if(bytes_to_send == 0)
    {
        printf("no data to send\n");
        modfd( m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(1)
    {
        temp = writev( m_sockfd, m_iv, m_iv_count);
        if(m_iv_count == 1)
        {
            printf("i send what\n %s\n",m_iv[0].iov_base);
        }
        else if(m_iv_count == 2)
        {
            printf("i send what\n %s\n %s\n",m_iv[0].iov_base, m_iv[1].iov_base);
        }
        else
        {
            printf("m_iv_count == 0\n");
        }
        
        
        
        if( temp <= -1)
        {
            //if TCP write buffer got no space
            //wait for the next EPOLLOUT event
            if(errno == EAGAIN)
            {
                printf("no enough space for writev, wait for next time\n");
                modfd( m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            printf("something wrong in writev\n");
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send +=temp;

        //respon successfully
        if(bytes_to_send <= bytes_have_send)
        {   
            printf("write finished\n");
            unmap();
            // if keep alive
            // re-init connection
            if( m_linger)
            {
                printf("dont close, keep alive!\n");
                init();
                modfd( m_epollfd, m_sockfd, EPOLLIN);
                return true;
            }
            //otherwise
            else
            {
                printf("close the connection\n");
                modfd(m_epollfd,m_sockfd, EPOLLIN);
                return false;
            }
            
        }
    }
}

//write data to write buf
bool http_conn::add_response(const char* format, ...)
{
    if( m_write_idx >= WRITE_BUFFER_SIZE)
    {
        printf("write data size exceeds the limit\n");
        return false;
    }

    va_list arg_list;
    va_start( arg_list,format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE-1-m_write_idx,
                format,arg_list);
    if( len >= (WRITE_BUFFER_SIZE -1 -m_write_idx))
    {
        return false;
    }
    m_write_idx +=len;
    va_end(arg_list);
    return true;
}


bool http_conn::add_status_line ( int status,const char* title)
{
    return add_response("%s %d %s\r\n","HTTP/1.1", status,title);
}

bool http_conn::add_headers( int content_len)
{
    add_content_length( content_len );
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length( int content_len )
{
    return add_response( "Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", 
        (m_linger == true)?"keep-alive":"close");
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n");
}

bool http_conn::add_content( const char* content)
{
    return add_response("%s",content);
}


//accroding to the process result
//response to the client
bool http_conn::process_write(HTTP_CODE ret)
{
    switch(ret)
    {
        case INTERNAL_ERROR:
        {
            add_status_line( 500, error_500_title);
            add_headers( strlen( error_500_form));
            if( !add_content( error_500_form) )
            {
                printf("in INTERNAL_ERROR, add content failed\n");
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(400, error_400_title);
            add_headers( strlen(error_400_form));
            if( !add_content( error_400_form))
            {
                printf("in BAD_REQUEST, add content failed\n");
                return false;
            }
            break;
        }

        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if( ! add_content(error_403_form))
            {
                printf("in FORBIDDEN_REQUEST, add content failed\n");
                return false; 
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line( 200 , ok_200_title);
            if( m_file_stat.st_size != 0)
            {
                add_headers( m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else
            {
                const char* ok_string = "<html><body></body></html>";
                add_headers( strlen(ok_string));
                if( !add_content(ok_string))
                {
                    return false;
                }
            }
            break;
            
        }

        case DATABASE_REQUEST:
        {
            add_status_line( 200 , ok_200_title);




            break;
        }

        case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if( ! add_content(error_404_form))
            {
                printf("in NO_RESOURCE, add content failed\n");
                return false; 
            }
            break;           
        }

        default:
        {
            return false;
        }
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}



//called by threads in threadpool
//the http request entry
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if( read_ret == NO_REQUEST)
    {
        modfd(m_epollfd,m_sockfd, EPOLLIN);
        return;
    }
    printf("read_ret is %d\n",read_ret);
    printf("AFTER PROCESS READ, NOW PREOCESS WRITE\n");
    bool write_ret = process_write(read_ret);
    if(!write_ret)
    {
        printf("write_ret false\n");
        //close the connection
        close_conn();
    }

    modfd(m_epollfd,m_sockfd,EPOLLOUT);

}
