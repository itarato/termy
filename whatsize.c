#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void) {
  struct winsize ws;
  if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1) {
    perror("Error: failed getting winsize.\n");
    exit(EXIT_FAILURE);
  }

  printf("TTY size: %u (w) x %u (h) .\n", ws.ws_col, ws.ws_row);

  return 0;
}
