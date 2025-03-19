//#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/select.h>
#include "server.h"


char root[PATHSIZE] = "./www";
int running = 1;
int sock = -1;
int threads_count = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;



void handle_signal(int sig) {
	printf("\nGot signal, shutting down the server\n");
	running = 0;
	if (sock != -1) {
		close(sock);
	}
	exit(sig);
}

void fix_path(char *path) {
	if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
		strcpy(path, "/index.html");
		return;
	}
	char *ptr;
	while ((ptr = strstr(path, "..")) != NULL) {
		*ptr = '\0';
	}
}

void parse_req(char *buf, Request *req, int client_sock) {
	memset(req, 0, sizeof(Request));
	req->keep_alive = 0;

	char *line = strtok(buf, "\r\n");
	if (!line) {
		printf("Improper request - no data\n");
		send_err(client_sock, 400, "HTTP/1.1"); //sending the response with default version as 1.1
		req->version[0] = '\0';
		return;
	}

	int items = sscanf(line, "%s %s %s", req->method, req->path, req->version);
	if (items != 3) {
		printf("Improper request - wrong format\n");
		send_err(client_sock, 400, "HTTP/1.1"); //sending the response with default version as 1.1
		req->version[0] = '\0';
		return;
	}

	if (strcmp(req->version, "HTTP/1.0") != 0
			&& strcmp(req->version, "HTTP/1.1") != 0) {
		printf("Bad version: %s\n", req->version); // other versio not supported
		send_err(client_sock, 505, "HTTP/1.1");
		req->version[0] = '\0';
		return;
	}

	// printf("HTTP version: %s\n", req->version);

	while ((line = strtok(NULL, "\r\n"))) {
		if (strlen(line) == 0)
			break;

		if (strncmp(line, "Host: ", 6) == 0) {
			strcpy(req->host, line + 6);
		} else if (strncmp(line, "Connection: ", 12) == 0) {
			printf("Connection header: %s\n", line + 12);
			//using strcasestr to find substring as this string may contain whitespaces and also is case insensitive
			if (strcasestr(line + 12, "keep-alive")) {
				req->keep_alive = 1;
			} else if (strcasestr(line + 12, "close")) {
				req->keep_alive = 0;
			}
		} else if (strncmp(line, "Content-Length: ", 16) == 0) {
			req->cont_len = atoi(line + 16);
		}
	}
	if (req->keep_alive == 0) {
		printf("Keep-alive: %s\n", "no");
		printf("Closing socket\n");
	} else {
		printf("Keep-alive: %s\n", "yes");
	}
}

void* handle_client(void *arg) {
	int sock = *(int*) arg;
	free(arg);

	char buf[BUFSIZE];
	Request req;
	int count = 0;
	struct timeval tv;
	time_t last_activity = time(NULL);

	// Setting the socket to non-blocking mode so as to get the timeout
	int flags = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, flags | O_NONBLOCK);

	memset(buf, 0, BUFSIZE);
	int n = recv(sock, buf, BUFSIZE - 1, 0);
	if (n <= 0) {
		printf("No initial request received: %s\n", strerror(errno));
		goto cleanup;
	}

	count++;
	last_activity = time(NULL);
	parse_req(buf, &req, sock);

	if (req.version[0] == '\0') {
		printf("Bad version in initial request\n");
		goto cleanup;
	}

	// Handle first request..This will not have a timeout as this is the first requet and as soon as we get connection information, the timeout will be applied accordingly
	if (strcmp(req.method, "GET") == 0) {
		handle_get(&req, sock);
	} else {
		send_err(sock, 405, req.version);
	}

	// if keep-alive then timeout if no new request within 10seconds
	while (req.keep_alive && running && count < MAX_REQS) {
		// Check timeout
		time_t current_time = time(NULL);
		time_t elapsed = current_time - last_activity;

		printf("Timeout check: elapsed time = %ld seconds\n", elapsed);

		if (elapsed >= TIMEOUT) {
			printf("Keep-alive timeout HAPPENED after %ld seconds\n", elapsed);
			break;
		}

		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);

		tv.tv_sec = TIMEOUT - elapsed;
		tv.tv_usec = 0;

		printf("Timeout started: waiting up to %ld seconds\n", tv.tv_sec);

		int ready = select(sock + 1, &readfds, NULL, NULL, &tv);

		if (ready < 0) {
			printf("Select error: %s\n", strerror(errno));
			break;
		}

		if (ready == 0) {
			printf("Timeout happended after %d seconds\n", TIMEOUT);
			break;
		}

		memset(buf, 0, BUFSIZE);
		n = recv(sock, buf, BUFSIZE - 1, 0);

		if (n <= 0) {
			if (n == 0) {
				printf("Connection closed by client\n");
			} else {
				printf("Recv error: %s\n", strerror(errno));
			}
			break;
		}

		// Resetting last activity time for new request
		last_activity = time(NULL);

		count++;
		parse_req(buf, &req, sock);

		if (req.version[0] == '\0')
			break;

		if (strcmp(req.method, "GET") == 0) {
			handle_get(&req, sock); // only handle get request
		} else {
			send_err(sock, 405, req.version);
		}
	}

	cleanup: close(sock);
	pthread_mutex_lock(&lock);
	threads_count--;
	pthread_mutex_unlock(&lock);
	return NULL;
}
void handle_get(Request *req, int sock) {
	fix_path(req->path);

	char path[PATHSIZE];
	sprintf(path, "%s%s", root, req->path);

	int len = strlen(path);
	while (len > 1 && path[len - 1] == '/') {
		path[--len] = '\0';
	}

	DIR *dir = opendir(path);
	if (dir) {
		closedir(dir);
		list_dir(sock, path, req, 1);
		return;
	}

	if (access(path, F_OK) != 0) {
		if (path[strlen(path) - 1] == '/') {   // directory path
			strcat(path, "index.html");
			if (access(path, F_OK) == 0) { // checking existence of index.html
				send_file(sock, path, get_type(path), 1, req->version,
						req->keep_alive);
				return;
			}
		}
		send_err(sock, 404, req->version); // not present
		return;
	}

	if (access(path, R_OK) != 0) {
		send_err(sock, 403, req->version);          // not readable
		return;
	}

	send_file(sock, path, get_type(path), 1, req->version, req->keep_alive);
}

void list_dir(int sock, char *path, Request *req, int send_body) {
	char index[PATHSIZE];

	sprintf(index, "%s/index.html", path);
	if (access(index, R_OK) == 0) {
		send_file(sock, index, get_type(index), send_body, req->version,
				req->keep_alive);
		return;
	}

	DIR *dir = opendir(path);
	if (!dir) {
		send_err(sock, 403, req->version);
		return;
	}

	char *list = malloc(BUFSIZE);
	int pos = 0;

	pos += sprintf(list + pos, "<html><head><title>Files</title></head>"
			"<body><h1>Files in directory:</h1><ul>");

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] != '.') {
			pos += sprintf(list + pos, "<li><a href=\"%s%s\">%s</a></li>",
					req->path, entry->d_name, entry->d_name);
		}
	}

	pos += sprintf(list + pos, "</ul></body></html>");

	char headers[512];
	sprintf(headers, "%s 200 OK\r\n"
			"Content-Type: text/html\r\n"
			"Content-Length: %d\r\n"
			"Connection: %s\r\n"
			"%s"
			"\r\n", req->version, pos, req->keep_alive ? "keep-alive" : "close",
			req->keep_alive ? "Keep-Alive: timeout=10\r\n" : "");

	send(sock, headers, strlen(headers), 0);

	if (send_body) {
		send(sock, list, pos, 0);
	}

	free(list);
	closedir(dir);
}

void send_err(int sock, int code, char *version) {
	char resp[BUFSIZE];
	char body[1024];
	char *msg;

	switch (code) {
	case 400:
		msg = "400 Bad Request";
		break;
	case 403:
		msg = "403 Forbidden";
		break;
	case 404:
		msg = "404 Not Found";
		break;
	case 405:
		msg = "405 Method Not Allowed";
		break;
	case 505:
		msg = "505 HTTP Version Not Supported";
		break;
	default:
		msg = "Error";
		break;      // may cause issues, adding just error to avoid issues
	}

	sprintf(body, "<html><body><h1>%s</h1></body></html>", msg);
	sprintf(resp, "%s %s\r\n"
			"Content-Type: text/html\r\n"
			"Content-Length: %ld\r\n"
			"Connection: close\r\n"
			"\r\n%s", version, msg, strlen(body), body);

	send(sock, resp, strlen(resp), 0);
}

void send_file(int sock, char *path, char *type, int send_body, char *version,
		int keep_alive) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		send_err(sock, 500, version);
		return;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	char headers[512];
	sprintf(headers, "%s 200 OK\r\n"
			"Content-Type: %s\r\n"
			"Content-Length: %ld\r\n"
			"Connection: %s\r\n"
			"%s"
			"\r\n", version, type, size, keep_alive ? "keep-alive" : "close",
			keep_alive ? "Keep-Alive: timeout=10\r\n" : "");

	send(sock, headers, strlen(headers), 0);

	if (send_body) {
		char buf[8192];
		int n;
		while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
			send(sock, buf, n, 0);
		}
	}

	fclose(f);
}

char* get_type(const char *file) {
	char *ext = strrchr(file, '.');
	if (!ext)
		return "text/plain";

	ext++; // to remove .

	if (!strcasecmp(ext, "html"))
		return "text/html";
	if (!strcasecmp(ext, "txt"))
		return "text/plain";
	if (!strcasecmp(ext, "png"))
		return "image/png";
	if (!strcasecmp(ext, "gif"))
		return "image/gif";
	if (!strcasecmp(ext, "jpg"))
		return "image/jpg";
	if(!strcasecmp(ext, ".ico"))
		return "image/x-icon";
	if (!strcasecmp(ext, "css"))
		return "text/css";
	if (!strcasecmp(ext, "js"))
		return "application/javascript";

	return "text/plain"; //adding some default as if there is a text/plain alreafy then it will send this which is correct ootherwise this is default
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		printf("Usage: %s <port>\n", argv[0]);
		return 1;
	}

	signal(SIGINT, handle_signal);

	int port = atoi(argv[1]);
	sock = socket(AF_INET, SOCK_STREAM, 0);

	if (sock < 0) {
		printf("Can't create socket\n");
		return 1;
	}

	int opt = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(port),
			.sin_addr.s_addr = INADDR_ANY };

	if (bind(sock, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
		printf("Can't bind\n");
		close(sock);
		return 1;
	}

	if (listen(sock, SOMAXCONN) < 0) { // using the default value which is maximum possible...
		printf("Can't listen\n");
		close(sock);
		return 1;
	}

	printf("Server started on port %d\n", port);

	while (running) {
		struct sockaddr_in client;
		socklen_t len = sizeof(client);

		int client_sock = accept(sock, (struct sockaddr*) &client, &len);

		if (client_sock < 0) {
			if (errno == EINTR)
				continue;
			printf("Accept failed\n");
			continue;
		}

		pthread_mutex_lock(&lock);
		if (threads_count >= MAX_THREADS) {
			pthread_mutex_unlock(&lock);
			close(client_sock);
			printf("Maximum Limit for threads achieved\n");
			continue;
		}
		threads_count++;
		pthread_mutex_unlock(&lock);

		int *arg = malloc(sizeof(int));
		*arg = client_sock;

		pthread_t tid;
		if (pthread_create(&tid, NULL, handle_client, arg) != 0) {
			free(arg);
			close(client_sock);
			pthread_mutex_lock(&lock);
			threads_count--;
			pthread_mutex_unlock(&lock);
			printf("Thread create failed\n");
			continue;
		}
		pthread_detach(tid);
	}

	close(sock);
	printf("Server stopped\n");
	return 0;
}

