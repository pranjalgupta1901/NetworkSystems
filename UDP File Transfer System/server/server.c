#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/wait.h>

#define BUFSIZE 40960
#define MAX_RETRIES 3

const char *eof = "END_OF_TRANSMISSION[][}{)(////";
const char *exit_msg = "GoodBye!\n";
const char *error_msg = "Error Error Error Error Error";

void error(const char *msg) {
    perror(msg);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *port = argv[1];
    int sockfd = -1;
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port, &hints, &res) != 0) {
        error("getaddrinfo failed");
        exit(EXIT_FAILURE);
    }

    printf("Listening on port %s\n", port);
    struct sockaddr_in clientaddr;
    unsigned char buf[BUFSIZE];
    socklen_t clientaddr_size = sizeof(clientaddr);

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        freeaddrinfo(res);
        error("ERROR opening socket");
        exit(EXIT_FAILURE);
    }

    if (bind(sockfd, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        error("Error in binding socket");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res);

    struct timeval receive_timeout = {50, 0};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout)) < 0) {
        perror("Error setting receive timeout");
    }

    while (1) {
    start:
        printf("\n\nWaiting for client command\n\n");
        memset(buf, 0, BUFSIZE);
        bool is_exit = false;
        bool is_put = false;
        bool is_delete = false;

        unsigned char read_buffer[BUFSIZE] = {0};
        char ack_msg = '\n';
        ssize_t bytes_read = 0;
        uint32_t seq_num = 1, received_seq_num = 0;
        bool packets_complete = false;
        int retry_count = 0;

        int received_len = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr*)&clientaddr, &clientaddr_size);
        if (received_len < 0) {
            continue;
        }

        buf[received_len] = '\0';

        char *args[5] = {0};
        int i = 0;
        char *token = strtok((char *)buf, " ");
        while (token != NULL && i < 5) {
            args[i++] = token;
            token = strtok(NULL, " ");
        }
        args[i] = NULL;

        if (!args[0]) {
            continue;
        }

        is_exit = (strcmp(args[0], "exit") == 0);
        is_put = (strcmp(args[0], "put") == 0);
        is_delete = (strcmp(args[0], "rm") == 0);

        if (is_exit) {
            memcpy((read_buffer + 4), exit_msg, strlen(exit_msg));
            bytes_read = strlen(exit_msg);
            goto send_exit_message;
        } else if (is_put) {
            if (!args[1]) {
                continue;
            }

            int fd = open(args[1], O_WRONLY | O_CREAT | O_TRUNC, 0777);
            if (fd == -1) {
                continue;
            }

            retry_count = 0;
            while (!packets_complete && retry_count < MAX_RETRIES) {
                memset(buf, 0, BUFSIZE);
                received_len = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr*)&clientaddr, &clientaddr_size);
                if (received_len < 0) {
                    retry_count++;
                    continue;
                }

                received_seq_num = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];

                if (seq_num == received_seq_num) {
                    if (sendto(sockfd, &ack_msg, 1, 0, (struct sockaddr*)&clientaddr, clientaddr_size) > 0) {
                        seq_num++;

                        if (strcmp((char *)(buf + 4), eof) == 0) {
                            packets_complete = true;
                            break;
                        }

                        if (write(fd, buf + 4, received_len - 4) != received_len - 4) {
                            break;
                        }
                        retry_count = 0;
                    }
                }
            }
            close(fd);
            if (retry_count >= MAX_RETRIES) {
                continue;
            }
            continue;
        }

        int pid = fork();
        if (pid == -1) {
            continue;
        }

        if (pid == 0) {
            int stdout_fd = open("temp_stdout.txt", O_WRONLY | O_CREAT | O_TRUNC, 0777);
            int stderr_fd = open("temp_stderr.txt", O_WRONLY | O_CREAT | O_TRUNC, 0777);
            
            if (stdout_fd == -1 || stderr_fd == -1) {
               perror("Unable to create stdout or stderr file");
               close(sockfd);
                exit(0);
            }

            if (dup2(stdout_fd, STDOUT_FILENO) == -1 || dup2(stderr_fd, STDERR_FILENO) == -1) {
                close(stdout_fd);
                close(stderr_fd);
                close(sockfd);
                exit(0);
            }

            close(stdout_fd);
            close(stderr_fd);

            execvp(args[0], args);
            perror("exec failed");
            close(sockfd);
            exit(0);
        }

        int wstatus;
        waitpid(pid, &wstatus, 0);

        int fd = open("temp_stdout.txt", O_RDONLY);
        int fd_err = open("temp_stderr.txt", O_RDONLY);
        
        if (fd == -1 || fd_err == -1) {
            if (fd != -1) close(fd);
            if (fd_err != -1) close(fd_err);
            goto start;
        }

        char buffer[BUFSIZE] = {0};
        char buffer_err[BUFSIZE] = {0};
        int out_fd = open("command_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0777);
        
        if (out_fd == -1) {
            close(fd);
            close(fd_err);
            goto start;
        }

        ssize_t bytes_read_stdout = 0;
        ssize_t bytes_read_stderr = 0;

        while (1) {
            bytes_read_stdout = read(fd, buffer, BUFSIZE-1);
            bytes_read_stderr = read(fd_err, buffer_err, BUFSIZE-1);

            if (bytes_read_stderr > 0) {
                write(out_fd, buffer_err, bytes_read_stderr);
                break;
            }

            if (bytes_read_stdout > 0) {
                write(out_fd, buffer, bytes_read_stdout);
            } else if (is_delete && WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) {
                write(out_fd, "File removed successfully\n", 25);
                break;
            } else if (bytes_read_stdout == 0) {
                if (!is_delete) break;
                write(out_fd, error_msg, strlen(error_msg));
                break;
            }
        }

        close(out_fd);
        close(fd);
        close(fd_err);

        unlink("temp_stdout.txt");
        unlink("temp_stderr.txt");

        fd = open("command_output.txt", O_RDONLY);
        if (fd == -1) {
            goto start;
        }

        retry_count = 0;
        while (1 && retry_count < MAX_RETRIES) {
            bzero(read_buffer, BUFSIZE);
            bytes_read = read(fd, read_buffer + 4, BUFSIZE - 4);
            if (bytes_read == 0) {
                goto send_eof_directly;
            }

send_exit_message:
            read_buffer[0] = seq_num & 0xFF;
            read_buffer[1] = (seq_num >> 8) & 0xFF;
            read_buffer[2] = (seq_num >> 16) & 0xFF;
            read_buffer[3] = (seq_num >> 24) & 0xFF;

            if (sendto(sockfd, read_buffer, bytes_read + 4, 0, (struct sockaddr*)&clientaddr, clientaddr_size) < 0) {
                retry_count++;
                continue;
            }

            int ack_len = recvfrom(sockfd, &ack_msg, 1, 0, (struct sockaddr*)&clientaddr, &clientaddr_size);
            if (ack_len < 0) {
                retry_count++;
                continue;
            }

            retry_count = 0;
            if (ack_msg == '\n') {
                seq_num++;
                if (is_exit) {
                    bzero(read_buffer, BUFSIZE);
                    is_exit = false;
                    goto send_eof_directly;
                }
            }

            if (strcmp(read_buffer + 4, eof) == 0) {
                break;
            }

            continue;

send_eof_directly:
            memcpy((char *)(read_buffer + 4), eof, strlen(eof));
            bytes_read = strlen(eof);
            goto send_exit_message;
        }
        close(fd);
    }
    return 0;
}
