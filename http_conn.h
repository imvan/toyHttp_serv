#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include "locker.h"


class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    
    static const int READ_BUFFER_SIZE = 2048;

    static const int WRITE_BUFFER_SIZE = 1024;

    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS,
                CONNECT, PATHC};
    
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0,
                    CHECK_STATE_HEADER,
                    CHECK_STATE_CONTENT};
    
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST,
                    NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST,
                    INTERNAL_ERROR, CLOSED_CONNECTION};

    enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};

public:
    void init(int sockfd, const sockaddr_in & addr);

    void close_conn(bool read_close = true);

    void process();

    bool read();

    bool write();

private:
    void init();

    //parse HTTP request
    HTTP_CODE process_read();

    //fill the HTTP response
    bool process_write ( HTTP_CODE ret);

    //set of funcs for parse request
    HTTP_CODE parse_request_line( char * text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    char* get_line() {return m_read_buf + m_start_line;}
    LINE_STATUS parse_line();


    //set of funcs for fill response
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content( const char* content);
    bool add_status_line( int status, const char* title);
    bool add_headers( int cotent_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;

private:
    int m_sockfd;
    sockaddr_in m_address;

    //read buffer
    char m_read_buf [READ_BUFFER_SIZE];

    //position of the data hasn't read
    int m_read_idx;

    //position of the data hasn't analysed;
    int m_checked_idx;

    //the line in parsing;
    int m_start_line;

    //write buffer
    char m_write_buf[WRITE_BUFFER_SIZE];

    //the bytes haven't send
    int m_write_idx;

    //the state of main state machine
    CHECK_STATE m_check_state;

    //request method
    METHOD m_method;

    //the target's path of the client requst
    //equels  doc_root + m_url
    // doc_root is the root of website
    char m_real_file [ FILENAME_LEN];

    //the request url
    char* m_url;

    //HTTP version
    char* m_version;

    //host name
    char* m_host;

    //http body length
    int m_content_length;

    //HTTP keepalive
    bool m_linger;

    //the posioton of target file which has been mmap to memory
    char* m_file_address;

    //the status of target file
    struct stat m_file_stat;

    //we use writev to process write
    
    struct iovec m_iv[2];
    int m_iv_count;
};


#endif