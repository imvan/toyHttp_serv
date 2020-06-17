#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd( int epollfd, int fd, bool one_shot);
extern int removefd( int epollfd, int fd);

void addsig( int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset( &sa, '\0',sizeof(sa));
    sa.sa_handler = handler;
    if(restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert( sigaction(sig,&sa,NULL) != -1);
}

void show_error( int connfd, const char* info)
{
    printf("%s",info);
    send(connfd, info, strlen(info),0);
    close(connfd);
}

int main(int argc, char* argv[])
{
    if( argc <= 2)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char *ip =argv[1];
    int port = atoi(argv[2]);

    //ignore SIGPIPE
    addsig(SIGPIPE, SIG_IGN);

    //create a threadpool
    threadpool<http_conn> * pool = NULL;
    try
    {
        pool = new threadpool<http_conn>; 
    }
    catch(...)
    {
        printf("threadpool create failed\n");
        return 1;
    }

    //pre-assign a http_conn to all client
    http_conn* users = new http_conn[MAX_FD];
    assert(users);
    int user_count = 0;

    int listenfd = socket( PF_INET,SOCK_STREAM,0);
    assert(listenfd >= 0);

    //when close, drop the write buf
    //send RST to client and reset self immediately
    //don't remain 2MSL timewait
    struct linger tmp = {1,0};
    setsockopt(listenfd, SOL_SOCKET,SO_LINGER, &tmp, sizeof(tmp));
    
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET,ip,&address.sin_addr);
    address.sin_port = htons(port);

    ret = bind(listenfd,(struct sockaddr*)&address,sizeof(address) );
    printf("ret is %d\n",ret);
    assert(ret >= 0);

    ret = listen(listenfd,5);
    assert(ret >= 0);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert( epollfd != -1);

    addfd( epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while(true)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER,-1);
        if( (number <0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }

        for( int i = 0; i<number; i++)
        {
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd)
            {
                printf("a new connection request!\n");
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd,(struct sockaddr*)&client_address,
                        &client_addrlength);
                
                if(connfd < 0)
                {
                    
                    printf("accpet fail, errno is: %d\n",errno);
                    continue;
                }
                if(http_conn::m_user_count >= MAX_FD)
                {
                    printf("too many users\n");
                    show_error(connfd, "Internal server busy");
                    continue;
                }
                users[connfd].init(connfd, client_address);
            }
            else if(events[i].events &(EPOLLRDHUP | EPOLLHUP| EPOLLERR))
            {
                printf("got a exception, close connection %d\n", sockfd);
                users[sockfd].close_conn();
            }

            else if(events[i].events & EPOLLIN)
            {
                //accroding the the ret of read
                //decede add task to threadpool
                // or close the connection
                if(users[sockfd].read())
                {
                    printf("append a new connection in to threadpool\n");
                    pool->append(users+sockfd);
                }
                else
                {   
                    printf("close the connection\n");
                    users[sockfd].close_conn();
                }             
            }

            else if( events[i].events & EPOLLOUT)
            {
                if(!users[sockfd].write())
                {
                    printf("write fail\n");
                    users[sockfd].close_conn();
                }
            }
            else
            {}
            

        }
    }

    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}