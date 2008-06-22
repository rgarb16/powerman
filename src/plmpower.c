/*****************************************************************************\
 *  $Id:$
 *****************************************************************************
 *  Copyright (C) 2007-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-2002-008.
 *  
 *  This file is part of PowerMan, a remote power management program.
 *  For details, see <http://www.llnl.gov/linux/powerman/>.
 *  
 *  PowerMan is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  PowerMan is distributed in the hope that it will be useful, but WITHOUT 
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License 
 *  for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with PowerMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

/* plmpower.c - control Insteon devices via SmartLabs PLM 2412S */

/* For PLM bits, see 'Insteon Modem Developer's Guide',
 *   http://www.smarthome.com/manuals/2412sdevguide.pdf
 * For general Insteon protocol, see 'Insteon: The Details'
 *   http://www.insteon.net/pdf/insteondetails.pdf
 * For Insteon commands, see 'EZBridge Reference Manual', Appendix B.
 *   http://www.simplehomenet.com/Downloads/EZBridge%20Manual.pdf
 * Also: 'Quick Reference Guide for Smarthome Device Manager for INSTEON'
 *   http://www.insteon.net/sdk/files/dm/docs/
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <libgen.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>

#include "xtypes.h"
#include "xmalloc.h"
#include "error.h"
#include "argv.h"
#include "xread.h"

#define IM_STX      0x02
#define IM_INFO     0x60
#define IM_SEND     0x62
#define IM_RESET    0x67
#define IM_ACK      0x06
#define IM_NAK      0x15
#define IM_RECV_STD 0x50
#define IM_RECV_EXT 0x51

/* insteon commands */
#define CMD_GRP_ASSIGN  0x01
#define CMD_GRP_DELETE  0x02
#define CMD_PING        0x10
#define CMD_ON          0x11
#define CMD_ON_FAST     0x12
#define CMD_OFF         0x13
#define CMD_OFF_FAST    0x14
#define CMD_BRIGHT      0x15
#define CMD_DIM         0x16
#define CMD_MAN_START   0x17
#define CMD_MAN_STOP    0x18
#define CMD_STATUS      0x19

typedef enum { ON, OFF, STATUS } cmd_t;

typedef struct {
    char h;
    char m;
    char l;
} addr_t;

#define OPTIONS "d:"
static struct option longopts[] = {
    { "device", required_argument, 0, 'd' },
    {0,0,0,0},
};

static void 
help(void)
{
    printf("Valid commands are:\n");
    printf("  info             get PLM info\n");
    printf("  reset            reset the PLM\n");
    printf("  on     xx.xx.xx  turn on device\n");
    printf("  off    xx.xx.xx  turn on device\n");
    printf("  status xx.xx.xx  query status of device\n");
}

static int 
str2addr(char *s, addr_t *ap)
{
    if (sscanf(s, "%2hhx.%2hhx.%2hhx", &ap->h, &ap->m, &ap->l) != 3) {
        printf("Error parsing address\n");
        return 0;
    }
    return 1;
}
static char *
addr2str(addr_t *ap)
{
    static char s[64];
    sprintf(s, "%.2hhX.%.2hhX.%.2hhX", ap->h, ap->m, ap->l);
    return s;
}

static void
plm_reset(int fd)
{
    char send[2] = { IM_STX, IM_RESET };
    char recv[3];

    xwrite_all(fd, send, sizeof(send));
    xread_all(fd, recv, sizeof(recv));
    switch (recv[2]) {
        case IM_ACK:
            printf("PLM reset complete\n");
            break;
        case IM_NAK:
        default:
            err_exit(FALSE, "plm_reset: failed");
    } 
}

static void
plm_info(int fd)
{
    addr_t addr;
    char send[2] = { IM_STX, IM_INFO };
    char recv[9];

    xwrite_all(fd, send, sizeof(send));
    xread_all(fd, recv, sizeof(recv));
    switch (recv[8]) {
        case IM_ACK:
            addr.h = recv[2];
            addr.m = recv[3];
            addr.l = recv[4];
            printf("id=%s dev=%.2hhX.%.2hhX vers=%hhd\n", addr2str(&addr), 
                    recv[5], recv[6], recv[7]);
            break;
        case IM_NAK:
        default:
            err_exit(FALSE, "plm_info: failed\n");
    }
}

static void
plm_send(int fd, addr_t *ap, char cmd1, char cmd2)
{
    char send[8] = { IM_STX, IM_SEND, ap->h, ap->m, ap->l, 3, cmd1, cmd2 };
    char recv[9];

    xwrite_all(fd, send, sizeof(send));
    xread_all(fd, recv, sizeof(recv));
    switch (recv[8]) {
        case IM_ACK:
            break;
        default:
        case IM_NAK:
            err_exit(FALSE, "plm_send: failed\n");
    }
}

static void
plm_recv(int fd, addr_t *ap, char *cmd1, char *cmd2)
{
    char recv[11];

    xread_all(fd, recv, sizeof(recv));
    if (recv[0] != IM_STX)
        err_exit(FALSE, "plm_recv: unexpected first byte: %.2hhX", recv[0]);
    switch (recv[1]) {
        case IM_RECV_STD:
            if (ap->h != recv[2] || ap->m != recv[3] || ap->l != recv[4])
                err_exit(FALSE, "plm_recv: unexpected from addr: %s",
                        addr2str(ap));
            if (cmd1)
                *cmd1 = recv[9];
            if (cmd2)
                *cmd2 = recv[10];
            break;
        default:
            err_exit(FALSE, "plm_recv: unexpected command: %.2hhX", recv[1]);
    }
}

static void
plm_on(int fd, char *addrstr)
{
    addr_t addr;    
    char cmd2;

    if (!str2addr(addrstr, &addr))
        return;
    plm_send(fd, &addr, CMD_ON_FAST, 0xff);
    plm_recv(fd, &addr, NULL, &cmd2);
    if (cmd2 == 0)
        err_exit(FALSE, "on command failed");
}

static void
plm_off(int fd, char *addrstr)
{
    addr_t addr;
    char cmd2;

    if (!str2addr(addrstr, &addr))
        return;
    plm_send(fd, &addr, CMD_OFF_FAST, 0);
    plm_recv(fd, &addr, NULL, &cmd2);
    if (cmd2 != 0)
        err_exit(FALSE, "off command failed");
}

static void
plm_status(int fd, char *addrstr)
{
    addr_t addr;
    char cmd2;

    if (!str2addr(addrstr, &addr))
        return;
    plm_send(fd, &addr, CMD_STATUS, 0);
    plm_recv(fd, &addr, NULL, &cmd2);
    printf("%s: %.2hhX\n", addrstr, cmd2);
}

void 
docmd(int fd, char **av, int *quitp)
{
    if (av[0] != NULL) {
        if (strcmp(av[0], "help") == 0) {
            help();
        } else if (strcmp(av[0], "quit") == 0) {
            *quitp = 1;
        } else if (strcmp(av[0], "info") == 0) {
            plm_info(fd);
        } else if (strcmp(av[0], "reset") == 0) {
            plm_reset(fd);
        } else if (strcmp(av[0], "on") == 0) {
            if (argv_length(av) != 2)
                printf("Usage: on xx.xx.xx\n");
            plm_on(fd, av[1]);
        } else if (strcmp(av[0], "off") == 0) {
            if (argv_length(av) != 2)
                printf("Usage: off xx.xx.xx\n");
            plm_off(fd, av[1]);
        } else if (strcmp(av[0], "status") == 0) {
            if (argv_length(av) != 2)
                printf("Usage: status xx.xx.xx\n");
            plm_status(fd, av[1]);
        } else 
            printf("type \"help\" for a list of commands\n");
    }
}

void shell(int fd)
{
    char buf[128];
    char **av;
    int quit = 0;

    while (!quit) {
        printf("plmpower> ");
        fflush(stdout);
        if (fgets(buf, sizeof(buf), stdin)) {
            av = argv_create(buf, "");
            docmd(fd, av, &quit);
            argv_destroy(av);
        } else
            quit = 1;
    }
}

void
usage(void)
{
    fprintf(stderr, "Usage: plmpower -d device \n");
    exit(1);
}

static int
open_serial(char *dev)
{
    struct termios tio;
    int fd;

    fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0)
        err_exit(TRUE, "could not open %s", dev);
    if (lockf(fd, F_TLOCK, 0) < 0) /* see comment in device_serial.c */
        err_exit(TRUE, "could not lock %s", dev);
    tcgetattr(fd, &tio);
    tio.c_cflag |= B19200 | CS8 | CLOCAL | CREAD;
    tio.c_iflag = IGNBRK | IGNPAR;
    tio.c_oflag = ONLRET;
    tio.c_lflag = 0;
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 1;
    tcsetattr(fd, TCSANOW, &tio);

    return fd;
}

int
main(int argc, char *argv[])
{
    char *device = NULL;
    int c;
    int fd;

    err_init(basename(argv[0]));

    while ((c = getopt_long(argc, argv, OPTIONS, longopts, NULL)) != EOF) {
        switch (c) {
            case 'd':
                device = optarg;
                break;
            default:
                usage();
                break;
        }
    }
    if (optind < argc)
        usage();
    if (device == NULL)
        usage();

    fd = open_serial(device);

    shell(fd);

    if (close(fd) < 0)
        err_exit(TRUE, "error closing %s", device);
    exit(0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
