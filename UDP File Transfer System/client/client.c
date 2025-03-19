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

#define BUFSIZE 1024*40

const char *eof  = "END_OF_TRANSMISSION[][}{)(////";
const char *exit_msg = "GoodBye!\n";
const char *error_msg = "Error Error Error Error Error"; 

int verify_syntax(char *command, char *actual_cmd, int len);

void error(char *str) {
    perror(str);
}

#define MAX_ATTEMPTS_ALLOWED 3 

int attempts = 0;

int receive_with_retry(int sockfd, unsigned char *buf, size_t len, struct sockaddr *serveraddr, socklen_t *serveraddr_size) {
    ssize_t bytes_read;
    bytes_read = recvfrom(sockfd, buf, len, 0, serveraddr, serveraddr_size);
    if (bytes_read > 0) 
       return bytes_read;
    return -1;
}

int send_with_retry(int sockfd, const unsigned char *buf, size_t len, const struct sockaddr *serveraddr, socklen_t serveraddr_size) {
    ssize_t sent_bytes;
    sent_bytes = sendto(sockfd, buf, len, 0, serveraddr, serveraddr_size);
    if (sent_bytes > 0) 
       return sent_bytes;
    return -1; 
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s <ip_address> <port>\n", argv[0]);
        exit(0);
    }

    bool is_put = false;
    bool is_get = false;
    char *ip_address = argv[1];
    bool exit_server = false;
    
    struct addrinfo hints, *res;
    int sockfd;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(argv[1], argv[2], &hints, &res) != 0) {
        perror("getaddrinfo failed");
        return -1;
    }
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        perror("socket creation failed");
        freeaddrinfo(res);
        return -1;
    }

    struct sockaddr_in serveraddr;
    memcpy(&serveraddr, res->ai_addr, res->ai_addrlen);
    socklen_t serveraddr_size = res->ai_addrlen;
    freeaddrinfo(res);

    while (1) {
    start: ;
        unsigned char buf[BUFSIZE] = {0};
        printf("\n\nPlease enter the command after choosing from the list given below:\n"
               "1. get [file_name]\n"
               "2. put [file_name]\n"
               "3. delete [file_name]\n"
               "4. ls\n"
               "5. exit\n");

        if (fgets(buf, BUFSIZE - 1, stdin) == NULL) {
            printf("Error reading input\n");
            continue;
        }

        struct timeval receive_timeout = {10, 0};
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout)) < 0) {
            perror("Error setting timeout");
            continue;
        }

        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }

        if (buf[0] == '\0') {
            printf("Please specify the command\n\n");
            goto start;
        }
        
        if (strncmp(buf, "put", 3) == 0) 
            is_put = true;
        else
            is_put = false;
            
        if (strncmp(buf, "get", 3) == 0) 
            is_get = true;
        else
            is_get = false;
            
        bool command_error = false;

        unsigned char temp_buf[BUFSIZE] = {0}, actual_buf[BUFSIZE] = {0};
        memcpy(temp_buf, buf, strlen(buf));

        int ret = verify_syntax(temp_buf, actual_buf, strlen(temp_buf));
        if (ret == -1) {
            printf("Syntax error in the entered command \"%s\", please correct it.\n", buf);
            goto start;
        } else if (ret == 2) {
            printf("Command not supported\n");
            goto start;
        }

        if(is_put) {
            char *filename = strtok(buf + 4, " ");
            if (!filename) {
                printf("No filename provided\n");
                continue;
            }

            int fd = open(filename, O_RDONLY);
            if (fd == -1) {
                error("Failed to open file for reading, please check if the file exists and also the permissions\n");
                goto start;
            }

            attempts = 0;
            while(attempts <= MAX_ATTEMPTS_ALLOWED) {
                int packets_sent_bytes = send_with_retry(sockfd, actual_buf, strlen(actual_buf), 
                                                       (struct sockaddr *)&serveraddr, serveraddr_size);
                if (packets_sent_bytes > 0) {
                    break;
                }
                attempts++;
                if(attempts > MAX_ATTEMPTS_ALLOWED) {
                    perror("error in sending command");
                    goto start;
                }
            }

            unsigned char read_buffer[BUFSIZE];
            ssize_t bytes_read;
            uint32_t seq_num = 1;
            char ack_msg = '\n';

            while (1) {
                bzero(read_buffer, BUFSIZE);
                bytes_read = read(fd, read_buffer + 4, BUFSIZE - 4);
                if (bytes_read == 0) {
                    memcpy(read_buffer + 4, eof, strlen(eof));
                    bytes_read = strlen(eof);
                }

                read_buffer[0] = seq_num & 0xFF;
                read_buffer[1] = (seq_num >> 8) & 0xFF;
                read_buffer[2] = (seq_num >> 16) & 0xFF;
                read_buffer[3] = (seq_num >> 24) & 0xFF;

                attempts = 0;
                bool chunk_sent = false;

                while (!chunk_sent && attempts <= MAX_ATTEMPTS_ALLOWED) {
                    int packets_sent_bytes = send_with_retry(sockfd, read_buffer, bytes_read + 4, 
                                                           (struct sockaddr *)&serveraddr, serveraddr_size);
                    if (packets_sent_bytes < 0) {
                        attempts++;
                        continue;
                    }

                    int ack_msg_len = receive_with_retry(sockfd, &ack_msg, 1, 
                                                       (struct sockaddr *)&serveraddr, &serveraddr_size);
                    if (ack_msg_len < 0) {
                        attempts++;
                        continue;
                    }

                    chunk_sent = true;
                }

                if (!chunk_sent) {
                    printf("Failed to send chunk after max attempts\n");
                    close(fd);
                    goto start;
                }

                if (ack_msg == '\n') {
                    seq_num++;
                }

                if (strcmp(read_buffer + 4, eof) == 0) {
                	printf("the file is uploaded successfully\n");
                    break;
                }
            }
            close(fd);
        } else {
            bool packets_complete = false;
            char ack_msg = '\n';
            uint32_t seq_num = 1;
            uint32_t received_seq_num = 0;
            attempts = 0;
           
            char requested_filename[256] = {0};
            if (strncmp(actual_buf, "cat ", 4) == 0) {
                strncpy(requested_filename, actual_buf + 4, sizeof(requested_filename) - 1);
            }

            while(attempts <= MAX_ATTEMPTS_ALLOWED) {
                int packets_sent_bytes = send_with_retry(sockfd, actual_buf, strlen(actual_buf), 
                                                       (struct sockaddr *)&serveraddr, serveraddr_size);
                if (packets_sent_bytes > 0) {
                    break;
                }
                attempts++;
                if(attempts > MAX_ATTEMPTS_ALLOWED) {
                    perror("error in sending command");
                    goto start;
                }
            }

          
            int file_1 = -1;
            if (is_get == true && strlen(requested_filename) > 0) {
                file_1 = open(requested_filename, O_WRONLY | O_CREAT | O_TRUNC, 0777);
            } else {
                file_1 = open("server_response.txt", O_WRONLY | O_CREAT | O_TRUNC, 0777);
            }

            if (file_1 == -1) {
                error("error in opening the file");
                goto start;
            }

            while (!packets_complete) {
                if (attempts > MAX_ATTEMPTS_ALLOWED) {
                    printf("ERROR: Max attempts reached for receive or ACK operation\n");
                    close(file_1);
                    goto start;
                }

                bzero(buf, BUFSIZE);
                int packets_len_received = receive_with_retry(sockfd, buf, BUFSIZE, 
                                                            (struct sockaddr *)&serveraddr, &serveraddr_size);
                if (packets_len_received < 0) {
                    attempts++;
                    continue;
                }

                received_seq_num = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
                if (seq_num != received_seq_num) {
                    attempts++;
                    continue;
                }

                if (send_with_retry(sockfd, &ack_msg, 1, (struct sockaddr *)&serveraddr, serveraddr_size) < 0) {
                    attempts++;
                    continue;
                }

                attempts = 0;

                if (memcmp(buf + 4, exit_msg, strlen(exit_msg)) == 0) {
                    exit_server = true;
                }

                if (memcmp(buf + 4, error_msg, strlen(error_msg)) == 0) {
                    command_error = true;
                }

                seq_num++;

                if (memcmp(buf + 4, eof, strlen(eof)) == 0) {
                    packets_complete = true;
                    break;
                }

                int bytes_write = write(file_1, buf + 4, packets_len_received - 4);
                if (bytes_write != packets_len_received - 4) {
                    error("error while writing the file");
                    close(file_1);
                    goto start;
                }
            }

            close(file_1);

            
            if (is_get != true) {
                file_1 = open("server_response.txt", O_RDONLY);
                if (file_1 == -1) {
                    error("Failed to open command output file");
                    goto start;
                }

                unsigned char read_buffer[BUFSIZE] = {0};
                ssize_t bytes_read;
                while(1) {
                    bytes_read = read(file_1, read_buffer, BUFSIZE);
                    if (bytes_read == 0) {
                        break;
                    }
                    if(command_error != true)
                        printf("%s", read_buffer);
                    else {
                        command_error = false;
                        printf("\n\n\nError occured! Please try your command again\n\n\n");
                    }
                }
                close(file_1);
            } else {
                if (!command_error) {
                    printf("File '%s' has been downloaded successfully.\n", requested_filename);
                } else {
                    printf("\n\n\nError occurred! Please try your command again\n\n\n");
                    unlink(requested_filename);
                }
            }

            if(exit_server == true) {
                close(sockfd);
                exit(0);
            }
        }
    }
    return 0;
}

int verify_syntax(char *command, char *actual_cmd, int len) {
    char command_copy[BUFSIZE];
    strncpy(command_copy, command, BUFSIZE - 1);
    command_copy[BUFSIZE - 1] = '\0';

    char *get_command = strtok(command_copy, " ");
    if (strcmp(get_command, "ls") == 0) {
        memcpy(actual_cmd, command, strlen(command));
        if (strtok(NULL, " ") != NULL) return -1;
    } else if (strcmp(get_command, "exit") == 0) {
        memcpy(actual_cmd, command, strlen(command));
        if (strtok(NULL, " ") != NULL) return -1;
    } else if (strcmp(get_command, "get") == 0) {
        memcpy(actual_cmd, "cat ", strlen("cat "));
        get_command = strtok(NULL, " ");
        if (get_command == NULL) return -1;
        strcat(actual_cmd, get_command);
    } else if (strcmp(get_command, "put") == 0) {
        memcpy(actual_cmd, "put ", strlen("put "));
        get_command = strtok(NULL, " ");
        if (get_command == NULL) return -1;
        strcat(actual_cmd, get_command);
    } else if (strcmp(get_command, "delete") == 0) {
        memcpy(actual_cmd, "rm ", strlen("rm "));
        get_command = strtok(NULL, " ");
        if (get_command == NULL) return -1;
        strcat(actual_cmd, get_command);
    } else {
        return 2;
    }
    actual_cmd[strlen(actual_cmd)] = '\0';
    return 0;
}
