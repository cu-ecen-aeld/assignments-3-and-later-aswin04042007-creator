#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>

int main(int argc, char *argv[]) {
    openlog(NULL, 0, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR, "Invalid argument count: expected 2, received %d", argc - 1);
        fprintf(stderr, "Usage: %s <file> <string>\n", argv[0]);
        closelog();
        return 1;
    }

    const char *filepath = argv[1];
    const char *writestr = argv[2];

    syslog(LOG_DEBUG, "Writing %s to %s", writestr, filepath);

    FILE *file = fopen(filepath, "w");
    if (!file) {
        syslog(LOG_ERR, "Failed to open %s: %s", filepath, strerror(errno));
        closelog();
        return 1;
    }

    if (fprintf(file, "%s", writestr) < 0) {
        syslog(LOG_ERR, "Failed to write to %s: %s", filepath, strerror(errno));
        fclose(file);
        closelog();
        return 1;
    }

    fclose(file);
    closelog();
    return 0;
}
