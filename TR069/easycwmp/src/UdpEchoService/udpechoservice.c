//==========================================================================
/**
 *  @file    udpechoservice.cpp
 *  @brief  The main codes for udp echo service
 *  @version 1.0
 *  @author Tian Yiqing <yiqing.tian@tcl.com>
 *  @date 2014-6-16
 */
//==========================================================================

#include <string.h>
#include "udpechoservice.h"
#include <net/if.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

int g_exit_flag = 0;
struct UdpEchoServiceConfig g_service_config;

static uint32_t g_response_failure_count = 0;
static uint32_t g_response_sn = 0;
static uint64_t g_packets_received = 0;
static uint64_t g_packets_responded = 0;
static uint64_t g_bytes_received = 0;
static uint64_t g_bytes_responded = 0;
static char g_first_packet_received_time[DATE_TIME_BUFFER_SIZE] = {0};
static char g_last_packet_received_time[DATE_TIME_BUFFER_SIZE] = {0};

static int g_is_first_packet = 1;

//Convert system time in secs to the date time string.
void get_current_sys_time(char *buffer, int buffer_size, struct timeval *tv)
{
    if(!buffer || buffer_size <= 0)
        return;

    struct tm *p = gmtime(&tv->tv_sec);
    snprintf(buffer, buffer_size - 1, "%04d-%02d-%02dT%02d:%02d:%02d.%ld", 1900+p->tm_year, 1+p->tm_mon, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec, tv->tv_usec);
    buffer[buffer_size - 1] = '\0';
}

void set_socket_opt(int fd)
{
    int flag = 1;
    socklen_t len = sizeof(flag);
    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, len) == -1)
    {
        PDEBUG("Set socket reuseaddr failed: %s\n", strerror(errno));
    }
}

//Get 32bits time stamp from 1900-1-1 00:00:00
uint32_t get_time_stamp(struct timeval *tv)
{
    uint64_t usecs = ((uint64_t)tv->tv_sec + JAN_1970)  * USEC_UNIT + (uint64_t)tv->tv_usec;
    return (uint32_t)usecs;
}

//Write the result to the output file
void output_result()
{
    char command[COMMAND_BUFFER_SIZE] = {0};
    snprintf(command, COMMAND_BUFFER_SIZE-1, "echo \"%llu %llu %llu %llu %s %s\" > %s", g_packets_received, g_packets_responded, g_bytes_received, g_bytes_responded, g_first_packet_received_time, g_last_packet_received_time, RESULT_FILE);
    command[COMMAND_BUFFER_SIZE -1] = '\0';
    system(command);
}

void service_main()
{
    struct UdpEchoPlusHeader *pEchoPlusHeader = NULL;
    char buffer[BUFFER_SIZE] = {0};
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    if(fd < 0)
    {
        PDEBUG("Create socket failed!\n");
        return;
    }

    //Get the binding ip address according to the interface
    if(strlen(g_service_config.if_name) > 0)
    {
        struct ifreq ifr;
        strncpy(ifr.ifr_name, g_service_config.if_name, IF_NAMESIZE);
        ifr.ifr_name[IFNAMSIZ-1]='\0';

        if (ioctl(fd, SIOCGIFADDR, &ifr) < 0)
        {
            PDEBUG("Getting interface address failed: %s\n", g_service_config.if_name);
        }
        else
        {
            struct sockaddr_in my_addr;
            memcpy(&my_addr, &ifr.ifr_addr, sizeof(my_addr));
            g_service_config.bind_addr.sin_addr = my_addr.sin_addr;
        }       

//        strncpy(ifr.ifr_name, g_service_config.if_name, IF_NAMESIZE);
//        if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, (char *)&ifr, sizeof(ifr)) < 0)
//        {

//        }
    }

     set_socket_opt(fd);

    if(bind(fd, (struct sockaddr*)&g_service_config.bind_addr, sizeof(g_service_config.bind_addr)))
    {
        PDEBUG("Bind to local address failed: %s\n", strerror(errno));
        return;
    }

    struct sockaddr_in src_addr;
    struct timeval tv;
    socklen_t len = sizeof(src_addr);
    for(;;)
    {        
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t ret = recvfrom(fd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&src_addr, &len);        
        gettimeofday(&tv, NULL);

        if(g_exit_flag)
        {
            PDEBUG("Service exit!\n");
            break;
        }

        if(ret > 0)
        {
            //If packets doesn't match source ip address should be dropped.
            if(memcmp(&src_addr.sin_addr, &g_service_config.dst_addr.sin_addr, sizeof(src_addr.sin_addr)) != 0)
            {
                PDEBUG("Recv unexpected packets from %s\n", inet_ntoa(src_addr.sin_addr));
                continue;
            }

            if(g_is_first_packet)
            {
                get_current_sys_time(g_first_packet_received_time, DATE_TIME_BUFFER_SIZE, &tv);
            }

            get_current_sys_time(g_last_packet_received_time, DATE_TIME_BUFFER_SIZE, &tv);

            g_packets_received++;
            g_bytes_received += ret;

            PDEBUG("Recv size: %d\n", ret);

            //Modify udp echo plus header.
            if(g_service_config.plus_enabled && ret > sizeof(struct UdpEchoPlusHeader))
            {
                pEchoPlusHeader = (struct UdpEchoPlusHeader *)buffer;

                pEchoPlusHeader->test_response_sn = htonl(g_response_sn);

                pEchoPlusHeader->test_response_reply_failure_count = htonl(g_response_failure_count);
                pEchoPlusHeader->test_response_recv_timestamp = htonl(get_time_stamp(&tv));

                gettimeofday(&tv, NULL);
                pEchoPlusHeader->test_response_reply_timestamp = htonl(get_time_stamp(&tv));
            }

            ret = sendto(fd, buffer, ret, 0, (struct sockaddr*)&src_addr, sizeof(src_addr));
            if(ret > 0)
            {
                g_packets_responded++;
                g_bytes_responded += ret;
                g_response_sn++;
            }
            else
            {
                g_response_failure_count += 1;
            }

            g_is_first_packet = 0;
            output_result();
            PDEBUG("Send size: %d\n", ret);
        }
        else if(ret < 0)
        {
            PDEBUG("Recv a error: %d, %s\n", errno, strerror(errno));
            if(errno == EINTR)
            {
                continue;
            }
            else
            {
                break;
            }
        }
        else
        {
            PDEBUG("Recv 0 bytes!!!\n");
            break;
        }
    }

    close(fd);
}

void client_main()
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    char buffer[1024] = {0};
    memset(buffer, 65, 1023);

    if(g_service_config.plus_enabled)
    {
        struct UdpEchoPlusHeader * pEchoPlusHeader = (struct UdpEchoPlusHeader *)buffer;
        pEchoPlusHeader->test_gen_sn = htonl(0);
    }

    int ret = sendto(fd, buffer, 1023, 0, (struct sockaddr*)&g_service_config.dst_addr, sizeof(g_service_config.dst_addr));
    PDEBUG("Send size: %d\n", ret);

    memset(buffer, 0, 1024);
    ret = recvfrom(fd, buffer, 1023, 0, NULL, NULL);

    PDEBUG("Recv size: %d\n", ret);
    if(g_service_config.plus_enabled && ret > sizeof(struct UdpEchoPlusHeader))
    {
        struct UdpEchoPlusHeader * pEchoPlusHeader = (struct UdpEchoPlusHeader *)buffer;
        PDEBUG("Recv plus header: %u, %u, %u, %u, %u\n",
               ntohl(pEchoPlusHeader->test_gen_sn),
               ntohl(pEchoPlusHeader->test_response_sn),
               ntohl(pEchoPlusHeader->test_response_recv_timestamp),
               ntohl(pEchoPlusHeader->test_response_reply_timestamp),
               ntohl(pEchoPlusHeader->test_response_reply_failure_count));
    }

    close(fd);
}
