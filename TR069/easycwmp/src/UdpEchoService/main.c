//==========================================================================
/**
 *  @file    main.cpp
 *  @brief  The entry of udp echo service
 *  @version 1.0
 *  @author Tian Yiqing <yiqing.tian@tcl.com>
 *  @date 2014-6-16
 */
//==========================================================================


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include "udpechoservice.h"

static int open_log = 0;

void term_handler(int n)
{
    g_exit_flag = 1;
}

int init_daemon(void)
{
#ifndef _DEBUG_
        __pid_t ret = fork();
        if(ret < 0)
        {
                printf("fork error: %s", strerror(errno));
                return -1;
        }
        else if(ret)
        {
                exit(0);
        }

        if(setsid() < 0)
        {
                printf("setsid error: %s", strerror(errno));
                return -1;
        }

        signal(SIGHUP, SIG_IGN);

        ret = fork();
        if(ret < 0)
        {
                printf("fork error: %s", strerror(errno));
                return -1;
        }
        else if(ret)
        {
                exit(0);
        }

        chdir("/");
        umask(0);
        int i;
        for(i=0; i<NOFILE; i++)
        {
                close(i);
        }

        open("/dev/null", O_RDONLY);
        open("/dev/null", O_RDWR);
        open("/dev/null", O_RDWR);

#endif

        if(open_log)
        {
            int log_fd = open(LOG_FILE, O_CREAT|O_RDWR|O_TRUNC, S_IRWXU);
            if(log_fd < 0)
            {
                    printf("Open log file failed!: %s\n", strerror(errno));
                    return -1;
            }

            close(STDOUT_FILENO);
            dup2(log_fd, STDOUT_FILENO);
            close(log_fd);
        }


        g_exit_flag = 0;

        struct sigaction act;
        act. sa_handler = term_handler;
        sigemptyset(&act.sa_mask);
        act. sa_flags = 0;
        sigaction(SIGINT, &act, NULL);
        sigaction(SIGTERM, &act, NULL);
        //signal(SIGINT, term_handler);
        //signal(SIGTERM, term_handler);
        signal(SIGPIPE, SIG_IGN);
        return 0;
}

int main(int argc, char *argv[])
{
    int result;
    int types = 0;
    int port = 0;
    int dst = 0;
    memset(&g_service_config, 0, sizeof(g_service_config));
    g_service_config.bind_addr.sin_family = AF_INET;
    g_service_config.bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    g_service_config.dst_addr.sin_family = AF_INET;

    while( (result = getopt(argc, argv, "sci:a:p:ed")) != -1 )
    {
        switch(result)
        {
        case 's':
            types = 1;
            break;
        case 'c':
            types = 0;
            break;
        case 'i':
            if(optarg)
            {
                strcpy(g_service_config.if_name, optarg);
            }
            break;
        case 'a':
            if(optarg == NULL)
            {
                dst = 0;
            }
            else if(inet_pton(AF_INET, optarg, &g_service_config.dst_addr.sin_addr) != 1)
            {
                dst = 0;
            }
            else
            {
                dst = 1;
            }

            break;
        case 'p':
            port = atoi(optarg);            
            break;
        case 'e':
            g_service_config.plus_enabled = 1;
            break;
        case 'd':
            open_log = 1;
            break;
        default:
            break;
        }
    }

    if(dst == 0)
    {
        PDEBUG("Please input dest ip address!\n");
        exit(1);
    }

    if(port <= 0)
    {
        PDEBUG("Please input port number!\n");
        exit(1);
    }   


    if(types == 1)
    {
        g_service_config.bind_addr.sin_port = htons(port);
        if(init_daemon())
        {
            PDEBUG("Init daemon failed!\n");
            exit(1);
        }
        service_main();
    }
    else
    {
        g_service_config.dst_addr.sin_port = htons(port);
        client_main();
    }

    return 0;
}
