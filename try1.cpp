#define _XOPEN_SOURCE 600

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define SLAVE_NAME_BUF_SIZE 64

using namespace std;

int open_master_pty(char *slave_name_buf, int slave_name_max_len) {
  int prev_errno;

  // Opening the unused master.
  int pty_master_fd = posix_openpt(O_RDWR | O_NOCTTY);
  if (pty_master_fd == -1) {
    printf("Error: cannot create master PTY.\n");
    return -1;
  }

  printf("Master PTY has been created, FD: %d.\n", pty_master_fd);

  // Change slave ownership and permission.
  int grantpt_result = grantpt(pty_master_fd);
  if (grantpt_result == -1) {
    printf("Error: failed updating slave ownership and perms.\n");

    prev_errno = errno;
    close(pty_master_fd);
    errno = prev_errno;

    return -1;
  }

  printf("Slave ownership and perms has been set. \n");

  int unlockpt_result = unlockpt(pty_master_fd);
  if (unlockpt_result == -1) {
    printf("Error: cannot unlock slave.\n");

    prev_errno = errno;
    close(pty_master_fd);
    errno = prev_errno;

    return -1;
  }

  printf("Slave unlocked.\n");

  char *slave_name = ptsname(pty_master_fd);
  if (slave_name == nullptr) {
    printf("Error: cannot obtain slave name.\n");

    prev_errno = errno;
    close(pty_master_fd);
    errno = prev_errno;

    return -1;
  }

  printf("Slave name: %s.\n", slave_name);

  int slave_name_len = strlen(slave_name);
  if (slave_name_len >= slave_name_max_len) {
    printf("Error: slave name is too large (%d), cannot fit into %d bytes.\n",
           slave_name_len, slave_name_max_len);

    close(pty_master_fd);
    errno = EOVERFLOW;

    return -1;
  }

  strncpy(slave_name_buf, slave_name, slave_name_len);

  return pty_master_fd;
}

pid_t pty_fork(int *master_pty_fd, char *slave_name, size_t slave_name_max_len,
               const struct termios *slave_termios,
               const struct winsize *slave_winsize) {
  char _slave_name_buf[SLAVE_NAME_BUF_SIZE];
  int _master_pty_fd = open_master_pty(_slave_name_buf, SLAVE_NAME_BUF_SIZE);
  if (_master_pty_fd == -1) {
    perror("Cannot open master pty\n");
    return -1;
  }

  if (slave_name != nullptr) {
    int slave_name_len = strlen(_slave_name_buf);
    if (slave_name_max_len > slave_name_len) {
      strncpy(slave_name, _slave_name_buf, slave_name_len);
    } else {
      printf("Error: cannot copy slave namem, too large.\n");

      close(_master_pty_fd);
      errno = EOVERFLOW;
      return -1;
    }
  }

  int prev_errno;

  pid_t child_pid = fork();
  if (child_pid == -1) {
    prev_errno = errno;
    close(_master_pty_fd);
    errno = prev_errno;

    return -1;
  }

  if (child_pid != 0) {  // Parent.
    *master_pty_fd = _master_pty_fd;
    return child_pid;
  }

  // Child.

  if (setsid() == -1) {
    perror("Error: cannot start session.\n");
    exit(EXIT_FAILURE);
  }

  close(_master_pty_fd);

  // Becoming controlling tty.
  int slave_pty_fd = open(slave_name, O_RDWR);
  if (slave_pty_fd == -1) {
    perror("Error: cannot open slave file.\n");
    exit(EXIT_FAILURE);
  }

#ifdef TIOCSCTTY
  // Becoming a controlling tty on BSD.
  if (ioctl(slave_pty_fd, TIOCSCTTY, 0) == -1) {
    perror("Error: cannot become controlling tty on BSD.\n");
    exit(EXIT_FAILURE);
  }
#endif

  if (slave_termios != nullptr) {
    if (tcsetattr(slave_pty_fd, TCSANOW, slave_termios) == -1) {
      perror("Error: cannot apply termios settings.\n");
      exit(EXIT_FAILURE);
    }
  }

  if (slave_winsize != nullptr) {
    if (ioctl(slave_pty_fd, TIOCSWINSZ, slave_winsize) == -1) {
      perror("Error: cannot set winsize.\n");
      exit(EXIT_FAILURE);
    }
  }

  if (dup2(slave_pty_fd, STDIN_FILENO) != STDIN_FILENO) {
    perror("Error: cannot clone stdin.\n");
    exit(EXIT_FAILURE);
  }
  if (dup2(slave_pty_fd, STDOUT_FILENO) != STDOUT_FILENO) {
    perror("Error: cannot clone stdout.\n");
    exit(EXIT_FAILURE);
  }
  if (dup2(slave_pty_fd, STDERR_FILENO) != STDERR_FILENO) {
    perror("Error: cannot clone stderr.\n");
    exit(EXIT_FAILURE);
  }

  if (slave_pty_fd > STDERR_FILENO) {
    close(slave_pty_fd);
  }

  return 0;
}

int main(void) { return 0; }
