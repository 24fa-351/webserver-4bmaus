#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define STATIC_DIR "static"
#define RESPONSE_HEADER                                                        \
  "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n"

static int total_requests = 0;
static size_t total_received_bytes = 0;
static size_t total_sent_bytes = 0;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

void handle_request(int client_socket);
void *client_handler(void *socket_desc);
void send_response(int client_socket, const char *content_type,
                   const char *content, size_t content_length);
void serve_static(int client_socket, const char *file_path);
void serve_stats(int client_socket);
void serve_calc(int client_socket, const char *query);
int parse_query_param(const char *query, const char *key);

void start_server(int port) {
  int server_socket, client_socket;
  struct sockaddr_in server_addr, client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  // Create socket
  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket < 0) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  // Bind
  if (bind(server_socket, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {
    perror("Bind failed");
    close(server_socket);
    exit(EXIT_FAILURE);
  }

  // Listen
  if (listen(server_socket, 10) < 0) {
    perror("Listen failed");
    close(server_socket);
    exit(EXIT_FAILURE);
  }

  printf("Server started on port %d\n", port);

  // Accept incoming connections
  while ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr,
                                 &client_addr_len))) {
    pthread_t thread;
    int *new_sock = malloc(1);
    *new_sock = client_socket;

    // Create a thread for each client
    if (pthread_create(&thread, NULL, client_handler, (void *)new_sock) < 0) {
      perror("Could not create thread");
      free(new_sock);
    }

    // Detach the thread
    pthread_detach(thread);
  }

  if (client_socket < 0) {
    perror("Accept failed");
  }

  close(server_socket);
}

void *client_handler(void *socket_desc) {
  int client_socket = *(int *)socket_desc;
  free(socket_desc);
  handle_request(client_socket);
  close(client_socket);
  return NULL;
}

void handle_request(int client_socket) {
  char buffer[BUFFER_SIZE];
  ssize_t received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
  if (received < 0) {
    perror("Receive failed");
    return;
  }
  buffer[received] = '\0';

  pthread_mutex_lock(&stats_mutex);
  total_requests++;
  total_received_bytes += received;
  pthread_mutex_unlock(&stats_mutex);

  // Check for different paths
  if (strncmp(buffer, "GET /static/", 12) == 0) {
    // Extract file path from request
    char file_path[BUFFER_SIZE];
    sscanf(buffer + 5, "%s", file_path);
    serve_static(client_socket, file_path);
  } else if (strncmp(buffer, "GET /stats", 10) == 0) {
    serve_stats(client_socket);
  } else if (strncmp(buffer, "GET /calc?", 10) == 0) {
    // Extract query from request
    char *query = strchr(buffer, '?') + 1;
    serve_calc(client_socket, query);
  } else {
    // Default response for unknown path
    const char *not_found =
        "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
    send(client_socket, not_found, strlen(not_found), 0);
  }
}

void send_response(int client_socket, const char *content_type,
                   const char *content, size_t content_length) {
  char header[BUFFER_SIZE];
  snprintf(header, sizeof(header), RESPONSE_HEADER, content_type,
           content_length);

  pthread_mutex_lock(&stats_mutex);
  total_sent_bytes += strlen(header) + content_length;
  pthread_mutex_unlock(&stats_mutex);

  send(client_socket, header, strlen(header), 0);
  send(client_socket, content, content_length, 0);
}

void serve_static(int client_socket, const char *file_path) {
  char full_path[BUFFER_SIZE];
  snprintf(full_path, sizeof(full_path), "%s/%s", STATIC_DIR,
           file_path + 7); // remove '/static' part

  FILE *file = fopen(full_path, "rb");
  if (!file) {
    const char *not_found =
        "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
    send(client_socket, not_found, strlen(not_found), 0);
    return;
  }

  fseek(file, 0, SEEK_END);
  size_t file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *file_content = malloc(file_size);
  fread(file_content, 1, file_size, file);
  fclose(file);

  send_response(client_socket, "application/octet-stream", file_content,
                file_size);
  free(file_content);
}

void serve_stats(int client_socket) {
  char content[BUFFER_SIZE];
  pthread_mutex_lock(&stats_mutex);
  snprintf(
      content, sizeof(content),
      "<html><body><h1>Server Statistics</h1><p>Total requests: %d</p><p>Total "
      "received bytes: %zu</p><p>Total sent bytes: %zu</p></body></html>",
      total_requests, total_received_bytes, total_sent_bytes);
  pthread_mutex_unlock(&stats_mutex);

  send_response(client_socket, "text/html", content, strlen(content));
}

void serve_calc(int client_socket, const char *query) {
  int a = parse_query_param(query, "a");
  int b = parse_query_param(query, "b");
  int sum = a + b;

  char content[BUFFER_SIZE];
  snprintf(content, sizeof(content),
           "<html><body><h1>Calculation Result</h1><p>%d + %d = "
           "%d</p></body></html>",
           a, b, sum);

  send_response(client_socket, "text/html", content, strlen(content));
}

int parse_query_param(const char *query, const char *key) {
  char *start = strstr(query, key);
  if (start) {
    return atoi(start + strlen(key) + 1);
  }
  return 0;
}
