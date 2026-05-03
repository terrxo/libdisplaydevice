/* apollo-evdi-helper.c
 *
 * Tiny setuid helper: performs a fixed set of evdi platform-device
 * lifecycle operations that require root, with zero attack surface.
 *
 * Usage:
 *   apollo-evdi-helper modprobe     -> insmod the evdi kernel module
 *   apollo-evdi-helper add          -> echo 1 > /sys/devices/evdi/add
 *   apollo-evdi-helper remove-all   -> echo 1 > /sys/devices/evdi/remove_all
 *
 * Exits with 0 on success, non-zero on failure.
 *
 * Install:
 *   sudo install -m 4755 -o root -g root apollo-evdi-helper /usr/local/bin/
 *
 * Build:
 *   cc -O2 -Wall -Wextra -Wpedantic -o apollo-evdi-helper apollo-evdi-helper.c
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int write_one_to(const char *path) {
  int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0) {
    perror(path);
    return 1;
  }
  ssize_t n = write(fd, "1", 1);
  int saved_errno = errno;
  close(fd);
  if (n != 1) {
    fprintf(stderr, "write to %s failed: %s\n", path, strerror(saved_errno));
    return 1;
  }
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s {modprobe|add|remove-all}\n", argv[0]);
    return 2;
  }
  /* Drop any inherited environment - we don't need it. */
  if (clearenv() != 0) {
    /* non-fatal, continue */
  }
  const char *cmd = argv[1];
  if (strcmp(cmd, "modprobe") == 0) {
    char *args[] = {(char *) "/usr/sbin/modprobe", (char *) "evdi", NULL};
    execv(args[0], args);
    /* alternate path */
    char *args2[] = {(char *) "/sbin/modprobe", (char *) "evdi", NULL};
    execv(args2[0], args2);
    perror("execv modprobe");
    return 1;
  }
  if (strcmp(cmd, "add") == 0) {
    return write_one_to("/sys/devices/evdi/add");
  }
  if (strcmp(cmd, "remove-all") == 0) {
    return write_one_to("/sys/devices/evdi/remove_all");
  }
  fprintf(stderr, "unknown command: %s\n", cmd);
  return 2;
}


