// seeded from https://www.binarytides.com/socket-programming-c-linux-tutorial/

#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Client.h"

int debug = 1;

#define LISTEN_PORT 8888
#define PENDING_CONNECTIONS_QUEUE_LENGTH 3
#define MAX_MESSAGE_LENGTH (10 * 1024 * 1024)
#define MAX_GENERATED_LENGTH 1024
#define MAX_FILESIZE 30 * 1024 * 1024

// Thread payload
typedef struct {
  Client *client;
} Thread_data;

// Takes a Thread_data*.
void *single_client_handler_threadfunc(void *);

// forward decls
//! All return FAIL (0). Anything else is successey
int establish_listening_socket(int port_to_listen);
int handle_new_client_wrapper(Client *cl);
int handle_new_client_guts(Client *cl);
int accept_a_client(int listen_socket, Client **new_client_ptr);
int close_down_listening(int listening_socket);
int read_http_request(int socket_fd, char **request_ptr);
int respond_to_http_request(Client *cl, char *request);
int send_http_response(Client *cl, char *body);
int handle_math_request(Client *cl, char *request);
int handle_static_request(Client *cl, char *request);

// this returns FAIL (system error - close connection), SUCCESS,
// or NONEXISTENT_FILE
int read_file_contents(const char *file_path, char *buf, int buffer_length);

int main(int argc, char *argv[]) {
  int our_socket_fd = establish_listening_socket(LISTEN_PORT);
  if (our_socket_fd == FAIL) {
    puts("exiting.");
    exit(1);
  }

  if (debug)
    puts("Ready for incoming connections...");

  int keep_going = SUCCESS;
  while (keep_going != FAIL) {
    Client *new_client;
    keep_going = accept_a_client(our_socket_fd, &new_client);

    if (keep_going != FAIL) {
      keep_going = handle_new_client_wrapper(new_client);
    }
  }

  close_down_listening(our_socket_fd);

  return 0;
}

// returns FAIL for failure, otherwise the fd to accept on
int establish_listening_socket(int port_to_listen) {
  int new_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (new_socket_fd == -1) {
    perror("Could not create socket");
    return FAIL;
  }
  if (debug)
    fprintf(stderr, "accept socket fd is %d\n", new_socket_fd);

  // We are going to listen on any address, the specified port
  struct sockaddr_in our_address;
  our_address.sin_family = AF_INET;
  our_address.sin_addr.s_addr = INADDR_ANY;
  our_address.sin_port = htons(port_to_listen);

  // Bind our socket to the given address
  if (bind(new_socket_fd, (struct sockaddr *)&our_address,
           sizeof(our_address)) < 0) {
    perror("bind failed");
    return FAIL;
  }
  if (debug)
    puts("bind done");

  // establish that we are expecting incoming connections
  int result = listen(new_socket_fd, PENDING_CONNECTIONS_QUEUE_LENGTH);
  if (result == -1) {
    perror("listen failed");
    return FAIL;
  }

  return new_socket_fd;
}

int accept_a_client(int listen_socket, Client **new_client_ptr) {
  struct sockaddr_in client_addr;
  // we must use a variable because accept() writes to it
  socklen_t sock_len = sizeof(client_addr);

  if (debug)
    fprintf(stderr, "accepting a connection on fd %d\n", listen_socket);

  int new_socket_fd =
      accept(listen_socket, (struct sockaddr *)&client_addr, &sock_len);
  if (new_socket_fd < 0) {
    perror("accept failed");
    return FAIL;
  }
  if (debug)
    fprintf(stderr, "Connection accepted. client fd is %d\n", new_socket_fd);

  Client *cl = client_new(new_socket_fd, &client_addr);
  *new_client_ptr = cl;
  return SUCCESS;
}

int close_down_listening(int listening_socket) {
  if (debug)
    fprintf(stderr, "closing socket fd %d\n", listening_socket);

  close(listening_socket);

  return SUCCESS;
}

// returns FAIL for error, 1 for success
//! Currently no "time to quit" handling
int handle_new_client_wrapper(Client *cl) {
  pthread_t client_handler_thread;

  // we must allocate this because we (probably) return before thread executes
  Thread_data *client_info = malloc(sizeof(Thread_data));

  client_info->client = cl;

  int result =
      pthread_create(&client_handler_thread,
                     NULL, // Use default thread attributes
                     single_client_handler_threadfunc, (void *)client_info);
  if (result < 0) {
    perror("pthread_create");
    return FAIL;
  }

  if (debug)
    fprintf(stderr, "Client handling thread is %lu\n", client_handler_thread);

  return SUCCESS;
}

// Payload ptr will be freed in this handler
void *single_client_handler_threadfunc(void *payload_ptr) {
  Client *client = ((Thread_data *)payload_ptr)->client;
  free(payload_ptr);

  int client_index = client_id(client);
  int result = handle_new_client_guts(client);

  if (debug)
    fprintf(stderr, "handle_new_client_guts (id %d) returned %d\n", client_index,
            result);

  return NULL;
}

int handle_new_client_guts(Client *client) {
  while (1) {
    char *request;
    int result = read_http_request(client_socket(client), &request);

    if (result == FAIL) {
      fprintf(stderr, "client %d read failed - closing, returning",
              client_id(client));
      client_free(client);
      return FAIL;
    }

    if (strlen(request) == 0) {
      fprintf(stderr, "client %d closed socket - closing, returning\n",
              client_id(client));
      client_free(client);
      free(request);
      return SUCCESS;
    }

    if (debug)
      fprintf(stderr,
              "client sent request (%d bytes): \n"
              "---\n"
              "%s\n"
              "---\n",
              result, request);

    result = respond_to_http_request(client, request);
    free(request);
    if (result == FAIL) {
      fprintf(stderr, "client %d response failed - closing, returning",
              client_id(client));
      client_free(client);
      return FAIL;
    }
  }
}

// technically, we're just reading whatever they send us.
//! Note, this fails for > MAX bytes
// This is quasi-intentional because using blocking reads
// and reading in chunks is more complicated than you
// might think.
int read_http_request(int socket_fd, char **request_ptr) {
  *request_ptr = malloc(MAX_MESSAGE_LENGTH + 1);

  int amount_read = read(socket_fd, *request_ptr, MAX_MESSAGE_LENGTH);

  if (amount_read < 0) {
    perror("read_http_request");
    free(*request_ptr);
    *request_ptr = NULL;
    return FAIL;
  }

  (*request_ptr)[amount_read] = '\0';
  *request_ptr = realloc(*request_ptr, amount_read + 1);

  if (amount_read == 0) {
    // client side closed connection
    if (debug)
      fputs("Client closed connection\n", stderr);

    return SUCCESS;
  }

  if (debug)
    fprintf(stderr, "Read %d bytes...\n", amount_read);

  return SUCCESS;
}

int send_http_response(Client *cl, char *body) {
  const char *canned_msg___fmt = "HTTP/1.1 200\n"
                                 "Content-type: text/plain\n"
                                 "Content-Length: %d\n"
                                 "Connection: Keep-Alive\n"
                                 "\n%s";

  // 100 = space for formatted %d
  int response_buffer_size = strlen(canned_msg___fmt) + 100 + strlen(body);
  char *response = malloc(response_buffer_size);

  snprintf(response, response_buffer_size, canned_msg___fmt, (int)strlen(body),
           body);

  int result = client_write(cl, response);
  free(response);

  return result;
}

int send_error_response(Client *cl) {
  return send_http_response(cl, "Invalid request.\n"
                                "\n"
                                "Not found.\n");
}

int respond_to_http_request(Client *cl, char *request) {
  if (!strncmp(request, "GET /plus/", 10))
    return handle_math_request(cl, request);
  if (!strncmp(request, "GET /static/", 10))
    return handle_static_request(cl, request);

  send_error_response(cl);
  return SUCCESS;
}

int handle_math_request(Client *cl, char *request) {
  int num1;
  int num2;
  int result = sscanf(request, "GET /plus/%d/%d ", &num1, &num2);

  if (result < 2 || result == EOF) {
    send_error_response(cl);
    return SUCCESS;
  }

  char response_body[MAX_GENERATED_LENGTH];
  snprintf(response_body, sizeof(response_body), "Sum of %d and %d is %d.\n",
           num1, num2, num1 + num2);

  return send_http_response(cl, response_body);
}

int handle_static_request(Client *cl, char *request) {
  char file_path[MAX_GENERATED_LENGTH];
  int result = sscanf(request, "GET /static/%s ", file_path);

  if (result < 1 || result == EOF) {
    send_error_response(cl);
    return SUCCESS;
  }

  char *file_contents = malloc(MAX_FILESIZE);

  result = read_file_contents(file_path, file_contents, MAX_FILESIZE);

  if (result == FAIL)
    return FAIL;
  if (result == NONEXISTENT_FILE) {
    return send_http_response(cl, "Nonexistent resource\n");
  }
  return send_http_response(cl, file_contents);
}

int file_size(FILE *fp) {
  // https://stackoverflow.com/questions/238603/how-can-i-get-a-files-size-in-c

  int current_position = ftell(fp);

  fseek(fp, 0L, SEEK_END);

  int file_sz = ftell(fp);

  fseek(fp, current_position, SEEK_SET);

  return file_sz;
}

// this returns FAIL (system error - close connection), SUCCESS,
// or NONEXISTENT_FILE
int read_file_contents(const char *file_path, char *buf, int buffer_length) {
  FILE *fp = fopen(file_path, "r");

  if (!fp) {
    return NONEXISTENT_FILE;
  }

  int result = fread(buf, sizeof(char), buffer_length, fp);

  fclose(fp);

  if (result == 0) {
    fprintf(stderr, "unable to read file '%s'.\n", file_path);
    return FAIL;
  }

  return SUCCESS;
}
