#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>

#define PORT "9000"
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUF_SIZE 1024

int server_sockfd = -1;
bool caught_sig = false;

// Signal handler for SIGINT and SIGTERM
void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        caught_sig = true;
        
        // Shut down the listening socket to unblock accept()
        if (server_sockfd > -1) {
            shutdown(server_sockfd, SHUT_RDWR);
            close(server_sockfd);
            server_sockfd = -1;
        }
    }
}

// Function to fork process and run in background
void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    if (setsid() < 0) {
        perror("setsid failed");
        exit(EXIT_FAILURE);
    }
    chdir("/");
    int dev_null = open("/dev/null", O_RDWR);
    if (dev_null >= 0) {
        dup2(dev_null, STDIN_FILENO);
        dup2(dev_null, STDOUT_FILENO);
        dup2(dev_null, STDERR_FILENO);
        close(dev_null);
    }
}

int main(int argc, char *argv[]) {
    bool run_daemon = false;
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        run_daemon = true;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Register signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Setup socket address structures
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &res) != 0) {
        syslog(LOG_ERR, "getaddrinfo failed");
        return -1;
    }

    server_sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_sockfd < 0) {
        syslog(LOG_ERR, "Socket creation failed");
        freeaddrinfo(res);
        return -1;
    }

    // Prevent 'Address already in use' errors
    int opt = 1;
    if (setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        syslog(LOG_ERR, "setsockopt failed");
        close(server_sockfd);
        freeaddrinfo(res);
        return -1;
    }

    if (bind(server_sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        syslog(LOG_ERR, "Bind failed");
        close(server_sockfd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);

    // Requirement: fork after ensuring it can bind to port 9000
    if (run_daemon) {
        daemonize();
    }

    if (listen(server_sockfd, BACKLOG) < 0) {
        syslog(LOG_ERR, "Listen failed");
        close(server_sockfd);
        return -1;
    }

    // Main accept loop
    while (!caught_sig) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_size = sizeof(client_addr);
        int client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr, &client_addr_size);

        if (caught_sig) {
            break;
        }

        if (client_sockfd < 0) {
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        size_t buffer_size = BUF_SIZE;
        char *rx_buffer = malloc(buffer_size);
        if (!rx_buffer) {
            syslog(LOG_ERR, "Malloc failed");
            close(client_sockfd);
            continue;
        }
        
        memset(rx_buffer, 0, buffer_size);
        size_t total_received = 0;
        bool newline_found = false;

        // Receive data until newline is found
        while (!newline_found && !caught_sig) {
            char temp_buf[BUF_SIZE];
            ssize_t bytes_recv = recv(client_sockfd, temp_buf, sizeof(temp_buf), 0);
            if (bytes_recv <= 0) break;

            if (total_received + bytes_recv > buffer_size) {
                buffer_size += BUF_SIZE + bytes_recv;
                char *new_buf = realloc(rx_buffer, buffer_size);
                if (!new_buf) {
                    syslog(LOG_ERR, "Realloc failed");
                    free(rx_buffer);
                    rx_buffer = NULL;
                    break;
                }
                rx_buffer = new_buf;
            }
            
            memcpy(rx_buffer + total_received, temp_buf, bytes_recv);
            total_received += bytes_recv;

            if (memchr(temp_buf, '\n', bytes_recv) != NULL) {
                newline_found = true;
            }
        }

        // Write to file and echo back
        if (rx_buffer && total_received > 0) {
            FILE *fp = fopen(DATA_FILE, "a+");
            if (fp) {
                fwrite(rx_buffer, 1, total_received, fp);
                
                fseek(fp, 0, SEEK_SET);
                char tx_buffer[BUF_SIZE];
                size_t bytes_read;
                while ((bytes_read = fread(tx_buffer, 1, sizeof(tx_buffer), fp)) > 0) {
                    send(client_sockfd, tx_buffer, bytes_read, 0);
                }
                fclose(fp);
            } else {
                syslog(LOG_ERR, "File open failed: %s", strerror(errno));
            }
        }

        if (rx_buffer) free(rx_buffer);
        
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_sockfd);
    }

    // Cleanup before exit
    remove(DATA_FILE);
    closelog();
    return 0;
}
