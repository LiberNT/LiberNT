/*
 * PROJECT:     LiberNT Linux subsystem tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test Linux route dump datagram framing
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */
#include <errno.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int test_dump(int fd, uint32_t port, uint32_t seq)
{
    struct {
        struct nlmsghdr header;
        struct rtmsg route;
        struct rtattr attribute;
        uint32_t table;
    } request = {0};
    struct sockaddr_nl kernel = { .nl_family = AF_NETLINK };
    unsigned char buffer[16384];
    int routes = 0;

    request.header.nlmsg_len = sizeof(request);
    request.header.nlmsg_type = RTM_GETROUTE;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.header.nlmsg_seq = seq;
    request.header.nlmsg_pid = port;
    request.route.rtm_family = AF_INET;
    request.attribute.rta_len = RTA_LENGTH(sizeof(request.table));
    request.attribute.rta_type = RTA_TABLE;
    request.table = RT_TABLE_MAIN;
    if (sendto(fd, &request, sizeof(request), 0, (struct sockaddr *)&kernel, sizeof(kernel)) != sizeof(request)) {
        perror("sendto");
        return 1;
    }
    for (;;) {
        ssize_t length = recv(fd, buffer, sizeof(buffer), 0);
        int remaining, messages = 0, done = 0;
        struct nlmsghdr *header;

        if (length <= 0 || length == sizeof(buffer)) {
            fprintf(stderr, "receive failed or truncated: %zd (%s)\n", length, strerror(errno));
            return 1;
        }
        remaining = (int)length;
        for (header = (struct nlmsghdr *)buffer; NLMSG_OK(header, remaining); header = NLMSG_NEXT(header, remaining)) {
            messages++;
            if (header->nlmsg_seq != seq || header->nlmsg_pid != port || !(header->nlmsg_flags & NLM_F_MULTI)) {
                fprintf(stderr, "bad reply header\n");
                return 1;
            }
            if (header->nlmsg_type == NLMSG_DONE) {
                int error;
                if (header->nlmsg_len < NLMSG_LENGTH(sizeof(error))) {
                    fprintf(stderr, "short completion\n");
                    return 1;
                }
                memcpy(&error, NLMSG_DATA(header), sizeof(error));
                if (error != 0) {
                    fprintf(stderr, "dump failed: %d\n", error);
                    return 1;
                }
                done++;
            } else if (header->nlmsg_type == RTM_NEWROUTE) {
                routes++;
            } else {
                fprintf(stderr, "unexpected message type %u\n", header->nlmsg_type);
                return 1;
            }
        }
        if (remaining != 0 || (done && (done != 1 || messages != 1))) {
            fprintf(stderr, "completion was not in its own datagram or message was malformed\n");
            return 1;
        }
        if (done) {
            if (routes == 0) {
                fprintf(stderr, "no IPv4 routes; cannot test datagram separation\n");
                return 77;
            }
            printf("sequence %u: %d routes, separate completion\n", seq, routes);
            return 0;
        }
    }
}

int main(void)
{
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    struct sockaddr_nl local = { .nl_family = AF_NETLINK };
    socklen_t length = sizeof(local);
    struct timeval timeout = { .tv_sec = 2 };
    int result;

    if (fd < 0 || bind(fd, (struct sockaddr *)&local, sizeof(local)) ||
        getsockname(fd, (struct sockaddr *)&local, &length) ||
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) {
        perror("netlink setup");
        return 1;
    }
    result = test_dump(fd, local.nl_pid, 1);
    if (result == 0)
        result = test_dump(fd, local.nl_pid, 2);
    close(fd);
    return result;
}
