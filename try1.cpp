#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>

int main(void) {
    int pty_master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (pty_master_fd == -1) {
        printf("Error: cannot create master PTY.\n");
        return 1;
    }

    printf("Master PTY has been created, FD: %d.\n", pty_master_fd);

    return 0;
}
