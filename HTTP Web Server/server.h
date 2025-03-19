#ifndef SERVER_H
#define SERVER_H


#define BUFSIZE 8192
#define PATHSIZE 512
#define MAX_THREADS 100
#define TIMEOUT 10
#define MAX_REQS 1000


typedef struct {
	char method[32];
	char path[PATHSIZE];
	char version[32];
	char host[100];
	char body[BUFSIZE];
	int cont_len;
	int keep_alive;
} Request;

void* handle_client(void *arg);
void parse_req(char *buf, Request *req, int client_sock);
void handle_get(Request *req, int client_sock);
void send_err(int sock, int code, char *version);
char* get_type(const char *file);
void send_file(int sock, char *path, char *type, int send_body, char *version,
		int keep_alive);
void list_dir(int sock, char *path, Request *req, int send_body);
void fix_path(char *path);


#endif // SERVER_H
