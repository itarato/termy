#define _XOPEN_SOURCE 600

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define SLAVE_NAME_BUF_SIZE 512
#define READ_BUF_SIZE 256
#define DEBUG_BUF_SIZE 1024

#define DBG(...) debug(__FILE__, __LINE__, __VA_ARGS__)

#define FAIL_IF_WITH_CODE(exp, msg) \
  if (exp) {                        \
    perror(msg);                    \
    exit(EXIT_FAILURE);             \
  }

#define FAIL_IF(exp, msg) \
  if (exp) {              \
    printf(msg);          \
    printf("\n");         \
    exit(EXIT_FAILURE);   \
  }

using namespace std;

struct termios tty_orig;
int global_master_pty_fd;

void debug(const char *file_name, int line_no, const char *msg, ...) {
  int f = open("pty.log", O_CREAT | O_APPEND | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  FAIL_IF_WITH_CODE(f == -1, "Cannot open debug file");

  int fmt_len;

  va_list args;
  va_start(args, msg);

  char fmt_buf[DEBUG_BUF_SIZE];
  const char *debug_fmt = "[\x1b[93m%s\x1b[39m:\x1b[96m%d\x1b[0m] %s\n";
  fmt_len = snprintf(fmt_buf, DEBUG_BUF_SIZE, debug_fmt, file_name, line_no, msg);
  FAIL_IF(fmt_len >= DEBUG_BUF_SIZE, "Debug fmt buffer overflow.");

  char buf[DEBUG_BUF_SIZE];

  int len = vsnprintf(buf, DEBUG_BUF_SIZE, fmt_buf, args);
  FAIL_IF(len >= DEBUG_BUF_SIZE, "Debug buffer overflow.");

  FAIL_IF_WITH_CODE(write(f, buf, strlen(buf)) == -1, "Cannot write to debug file");

  va_end(args);

  close(f);
}

int open_master_pty(char *slave_name_buf, int slave_name_max_len) {
  int prev_errno;

  // Opening the unused master.
  int pty_master_fd = posix_openpt(O_RDWR | O_NOCTTY);
  if (pty_master_fd == -1) {
    printf("Error: cannot create master PTY.\n");
    return -1;
  }

  global_master_pty_fd = dup(pty_master_fd);
  if (global_master_pty_fd == -1) {
    perror("Error: cannot dup master pty fd.\n");
    exit(EXIT_FAILURE);
  }

  DBG("Master PTY has been created, FD: %d.\n", pty_master_fd);

  // Change slave ownership and permission.
  int grantpt_result = grantpt(pty_master_fd);
  if (grantpt_result == -1) {
    printf("Error: failed updating slave ownership and perms.\n");

    prev_errno = errno;
    close(pty_master_fd);
    errno = prev_errno;

    return -1;
  }

  DBG("Slave ownership and perms has been set. \n");

  int unlockpt_result = unlockpt(pty_master_fd);
  if (unlockpt_result == -1) {
    printf("Error: cannot unlock slave.\n");

    prev_errno = errno;
    close(pty_master_fd);
    errno = prev_errno;

    return -1;
  }

  DBG("Slave unlocked.\n");

  char *slave_name = ptsname(pty_master_fd);
  if (slave_name == nullptr) {
    printf("Error: cannot obtain slave name.\n");

    prev_errno = errno;
    close(pty_master_fd);
    errno = prev_errno;

    return -1;
  }

  DBG("Slave name: %s.\n", slave_name);

  int slave_name_len = strlen(slave_name);
  if (slave_name_len >= slave_name_max_len) {
    printf("Error: slave name is too large (%d), cannot fit into %d bytes.\n", slave_name_len, slave_name_max_len);

    close(pty_master_fd);
    errno = EOVERFLOW;

    return -1;
  }

  strncpy(slave_name_buf, slave_name, slave_name_max_len);

  return pty_master_fd;
}

pid_t pty_fork(int *master_pty_fd, char *slave_name, size_t slave_name_max_len, const struct termios *slave_termios,
               const struct winsize *slave_winsize) {
  char _slave_name_buf[SLAVE_NAME_BUF_SIZE];
  int _master_pty_fd = open_master_pty(_slave_name_buf, SLAVE_NAME_BUF_SIZE);
  if (_master_pty_fd == -1) {
    perror("Cannot open master pty\n");
    return -1;
  }

  if (slave_name != nullptr) {
    int slave_name_len = strlen(_slave_name_buf);
    if (slave_name_max_len <= slave_name_len) {
      printf("Error: cannot copy slave name, too large.\n");

      close(_master_pty_fd);
      errno = EOVERFLOW;
      return -1;
    }

    strncpy(slave_name, _slave_name_buf, slave_name_max_len);
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
  int slave_pty_fd = open(_slave_name_buf, O_RDWR);
  if (slave_pty_fd == -1) {
    DBG("Slave file: %s\n", _slave_name_buf);
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

static void tty_reset(void) {
  if (tcsetattr(STDIN_FILENO, TCSANOW, &tty_orig) == -1) {
    printf("Error: failed resetting tty.\n");
    exit(EXIT_FAILURE);
  }
}

int tty_set_raw(int fd, struct termios *prev_termios) {
  struct termios t;

  if (tcgetattr(fd, &t) == -1) {
    printf("Error: cannot get tty config.\n");
    return -1;
  }

  if (prev_termios != nullptr) {
    *prev_termios = t;
  }

  t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO);
  t.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INLCR | INPCK | ISTRIP | IXON | PARMRK);

  t.c_oflag &= ~OPOST;

  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSAFLUSH, &t) == -1) {
    return -1;
  }

  return 0;
}

void sig_winch(int sig_no, siginfo_t *info, void *context) {
  DBG("Signal: %u.", sig_no);

  if (sig_no == SIGWINCH) {
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1) {
      perror("Error: failed reading winsize.\n");
      exit(EXIT_FAILURE);
    }

    DBG("Winsize: %u x %u.", ws.ws_row, ws.ws_col);

    if (ioctl(global_master_pty_fd, TIOCSWINSZ, &ws) == -1) {
      perror("Error: failed setting winsize for master pty.\n");
      exit(EXIT_FAILURE);
    }
  }
}

void setup_signal_handlers() {
  struct sigaction sa {
    0
  };

  sa.sa_flags = 0;
  sa.sa_sigaction = &sig_winch;

  if (sigaction(SIGWINCH, &sa, nullptr) == -1) {
    perror("Error: cannot set signal handlers.\n");
    exit(EXIT_FAILURE);
  }

  DBG("Signal handlers set.");
}

int start_pty(int pipe_reader) {
  struct winsize current_tty_winsize;
  if (ioctl(STDIN_FILENO, TIOCGWINSZ, &current_tty_winsize) < 0) {
    perror("Cannot get current tty winsize.\n");
    exit(EXIT_FAILURE);
  }

  int master_pty_fd;
  char slave_name[SLAVE_NAME_BUF_SIZE];
  pid_t child_pid = pty_fork(&master_pty_fd, slave_name, SLAVE_NAME_BUF_SIZE, &tty_orig, &current_tty_winsize);
  if (child_pid == -1) {
    perror("Error: cannot fork.\n");
    exit(EXIT_FAILURE);
  }

  char const *shell;
  if (child_pid == 0) {  // Child.
    shell = getenv("SHELL");
    if (shell == nullptr || *shell == '\0') {
      shell = "/bin/sh";
    }

    execlp(shell, shell, (char *)nullptr);

    // Should not get here in execution.
    printf("Child | Fatal: should not get here in code.\n");
    exit(EXIT_FAILURE);
  }

  // Parent process.

  setup_signal_handlers();

  fd_set in_fds;
  ssize_t read_len;
  char read_buf[READ_BUF_SIZE];

  for (;;) {
    FD_ZERO(&in_fds);
    FD_SET(pipe_reader, &in_fds);
    FD_SET(master_pty_fd, &in_fds);

    for (;;) {
      if (select(master_pty_fd + 1, &in_fds, nullptr, nullptr, nullptr) == -1) {
        if (errno == EINTR) continue;

        perror("Parent | Error: select failed for changes.\n");
        exit(EXIT_FAILURE);
      }

      break;
    }
    // printf("Parent | Select fired.\n");

    if (FD_ISSET(pipe_reader, &in_fds)) {  // STDIN --> PTY
      read_len = read(pipe_reader, read_buf, READ_BUF_SIZE);
      // printf("Parent | Select on STDIN: %d.\n", read_len);

      if (read_len <= 0) {
        break;
      }

      if (write(master_pty_fd, read_buf, read_len) != read_len) {
        printf("Parent | Error: invalid write len to master-pty-fd.\n");
        exit(EXIT_FAILURE);
      }
    }

    if (FD_ISSET(master_pty_fd, &in_fds)) {  // PTY --> STDOUT + file
      read_len = read(master_pty_fd, read_buf, READ_BUF_SIZE);
      // printf("Parent | Select on master- pty: %d.\n", read_len);

      if (read_len <= 0) {
        break;
      }

      if (write(STDOUT_FILENO, read_buf, read_len) != read_len) {
        printf("Parent | Error: invalid write len to stdout.\n");
        exit(EXIT_FAILURE);
      }
    }
  }

  exit(EXIT_SUCCESS);
}

int main(void) {
  if (tcgetattr(STDIN_FILENO, &tty_orig) == -1) {
    perror("Cannot fetch current tty settings.\n");
    exit(EXIT_FAILURE);
  }

  DBG("Set tty raw.\n");
  tty_set_raw(STDIN_FILENO, &tty_orig);

  if (atexit(tty_reset) != 0) {
    perror("Error: cannot set exit handler.\n");
    exit(EXIT_FAILURE);
  }

  int pipe_rw[2];
  if (pipe(pipe_rw) == -1) {
    perror("Failed pipe.");
    exit(EXIT_FAILURE);
  }

  int child_pid = fork();
  if (child_pid == -1) {
    perror("Master fork.");
    exit(EXIT_FAILURE);
  }

  if (child_pid == 0) {  // Child
    close(pipe_rw[1]);
    start_pty(pipe_rw[0]);
  }

  close(pipe_rw[0]);

  char buf[READ_BUF_SIZE];
  int read_len;
  for (;;) {
    read_len = read(STDIN_FILENO, buf, READ_BUF_SIZE);
    if (read_len == -1) {
      perror("Cannot read main STDIN.");
      exit(EXIT_FAILURE);
    }

    if (write(pipe_rw[1], buf, read_len) != read_len) {
      printf("Error: write error to pipe.\n");
      exit(EXIT_FAILURE);
    }
  }

  // Hitting ^[D keeps the process hanging. I think it's because the pty master is not getting a 0 read from pipe -
  // which would prompt it to `exit`.
  // Maybe: setup a SIGINT handler and close the pipe?
}
