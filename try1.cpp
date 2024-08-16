#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>

int main(void)
{
    // Opening the unused master.
    int pty_master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (pty_master_fd == -1)
    {
        printf("Error: cannot create master PTY.\n");
        return 1;
    }

    printf("Master PTY has been created, FD: %d.\n", pty_master_fd);

    // Change slave ownership and permission.
    int grantpt_result = grantpt(pty_master_fd);
    if (grantpt_result == -1)
    {
        printf("Error: failed updating slave ownership and perms.\n");
        return 2;
    }

    printf("Slave ownership and perms has been set. \n");

    int unlockpt_result = unlockpt(pty_master_fd);
    if (unlockpt_result == -1)
    {
        printf("Error: cannot unlock slave.\n");
        return 3;
    }

    printf("Slave unlocked.\n");

    char *slave_name = ptsname(pty_master_fd);
    if (slave_name == nullptr)
    {
        printf("Error: cannot obtain slave name.\n");
        return 4;
    }

    printf("Slave name: %s.\n", slave_name);

    return 0;
}
