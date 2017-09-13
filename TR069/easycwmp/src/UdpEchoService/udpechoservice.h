#ifndef UDPECHOSERVICE_H
#define UDPECHOSERVICE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stddef.h>

#define BUFFER_SIZE 8192
#define DATE_TIME_BUFFER_SIZE 32
#define COMMAND_BUFFER_SIZE 512
#define LOG_FILE "/var/log/udp_echo_service.log"
#define RESULT_FILE "/tmp/udp_echo_service_result.log"
#define PDEBUG(fmt, args...) printf("(D)UdpEchoService: [" __FILE__ ":%d] " fmt, __LINE__, ## args)
#define JAN_1970    0x83aa7e80LL
#define USEC_UNIT   1000000LL
struct UdpEchoServiceConfig
{
    int plus_enabled;
    char if_name[16];
    struct sockaddr_in dst_addr;
    struct sockaddr_in bind_addr;
};

struct UdpEchoPlusHeader
{
    uint32_t test_gen_sn;
    uint32_t test_response_sn;
    uint32_t test_response_recv_timestamp;
    uint32_t test_response_reply_timestamp;
    uint32_t test_response_reply_failure_count;
};

extern int g_exit_flag;
extern struct UdpEchoServiceConfig g_service_config;

void service_main();
void client_main();

#endif // UDPECHOSERVICE_H
