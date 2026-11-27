/*
 * replayer: plays a command file into the verifier through /dev/dtee.
 *
 * This is the stand-in for the real SLAM pipeline. ORB-SLAM3 runs on a PC and
 * writes a trajectory; tools/inject.py turns it into a command file; this
 * program sends each command at the right time and reports every verdict.
 *
 *   replayer /data/replay.cmd
 *
 * Output, one line per command (parsed by tools/score.py):
 *   R,<idx>,<seq>,<verdict>
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dtee_ioctl.h"

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: replayer <file.cmd>\n");
        return 2;
    }

    FILE *in = fopen(argv[1], "r");
    if (!in) {
        perror(argv[1]);
        return 1;
    }

    int fd = open("/dev/dtee", O_RDWR);
    if (fd < 0) {
        perror("open /dev/dtee");
        return 1;
    }

    struct dtee_stats before = { 0 }, after = { 0 };
    ioctl(fd, DTEE_IOC_STATS, &before);

    char line[256];
    long sent = 0, accepted = 0;
    unsigned counts[V_COUNT] = { 0 };

    while (fgets(line, sizeof(line), in)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;

        long idx, delay_us;
        unsigned long long seq;
        int x, y, z, yaw;
        unsigned mode;
        if (sscanf(line, "%ld,%ld,%llu,%d,%d,%d,%d,%u",
                   &idx, &delay_us, &seq, &x, &y, &z, &yaw, &mode) != 8) {
            fprintf(stderr, "bad line: %s", line);
            continue;
        }

        if (delay_us > 0)
            usleep(delay_us);

        struct dtee_submit s = { 0 };
        s.cmd.seq = seq;
        s.cmd.x_mm = x;
        s.cmd.y_mm = y;
        s.cmd.z_mm = z;
        s.cmd.yaw_mdeg = yaw;
        s.cmd.mode = mode;

        if (ioctl(fd, DTEE_IOC_SUBMIT, &s) != 0) {
            printf("R,%ld,%llu,ERR\n", idx, seq);
            continue;
        }

        printf("R,%ld,%llu,%u\n", idx, seq, s.verdict);
        sent++;
        if (s.verdict < V_COUNT)
            counts[s.verdict]++;
        if (s.verdict == V_ACCEPT)
            accepted++;
    }
    fclose(in);

    ioctl(fd, DTEE_IOC_STATS, &after);
    printf("END,sent=%ld,accepted=%ld,actuator_writes=%llu,failsafes=%llu\n",
           sent, accepted,
           (unsigned long long)(after.accepted - before.accepted),
           (unsigned long long)(after.failsafe - before.failsafe));
    printf("END,verdicts: ACCEPT=%u SEQ=%u RATE=%u MODE=%u FENCE=%u JUMP=%u SPEED=%u TIMEOUT=%u FAULT=%u\n",
           counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7], counts[8]);
    close(fd);
    return 0;
}
