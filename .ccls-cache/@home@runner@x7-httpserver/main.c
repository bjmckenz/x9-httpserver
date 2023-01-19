// seeded from https://www.binarytides.com/socket-programming-c-linux-tutorial/

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

int debug = 1;

#define LISTEN_PORT 8888
#define PENDING_CONNECTIONS_QUEUE_LENGTH 3
#define MAX_MESSAGE_LENGTH 10000

// Thread payload
typedef struct
{
  int socket_fd;
  struct sockaddr_in client_address;
} Thread_data;

// Takes a Thread_data*.
void *single_client_handler_threadfunc(void *);

// forward decls
#define FAIL 0
//! All return FAIL (0). Anything else is successey
int establish_listening_socket(int port_to_listen);
int handle_new_client_wrapper(int socket_fd, struct sockaddr_in client_addr);
int handle_new_client_guts(int socket_fd, struct sockaddr_in client_addr);
int accept_a_client(int listen_socket, struct sockaddr_in *client_addr_ptr);
int close_down_listening(int listening_socket);
int read_http_request(int socket_fd, char **request_ptr);
int respond_to_http_request(int socket_fd, char *request);
int send_http_response(int socket_fd, char *body);
int send_http_response(int socket_fd, char *body);

int main(int argc, char *argv[])
{
  int our_socket_fd = establish_listening_socket(LISTEN_PORT);
  if (our_socket_fd == FAIL)
  {
    puts("exiting.");
    exit(1);
  }

  if (debug)
    puts("Ready for incoming connections...");

  int keep_going = !FAIL;
  while (keep_going != FAIL)
  {
    struct sockaddr_in new_client_address;

    int new_client_fd = accept_a_client(our_socket_fd, &new_client_address);
    // (because a zero fd is a fail)
    keep_going = new_client_fd;

    if (keep_going != FAIL)
    {
      keep_going = handle_new_client_wrapper(new_client_fd,
                                             new_client_address);
    }
  }

  close_down_listening(our_socket_fd);

  return 0;
}

// returns FAIL for failure, otherwise the fd to accept on
int establish_listening_socket(int port_to_listen)
{
  int new_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (new_socket_fd == -1)
  {
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
  if (bind(new_socket_fd,
           (struct sockaddr *)&our_address,
           sizeof(our_address)) < 0)
  {
    perror("bind failed");
    return FAIL;
  }
  if (debug)
    puts("bind done");

  // establish that we are expecting incoming connections
  int result = listen(new_socket_fd, PENDING_CONNECTIONS_QUEUE_LENGTH);
  if (result == -1)
  {
    perror("listen failed");
    return FAIL;
  }

  return new_socket_fd;
}

int accept_a_client(int listen_socket, struct sockaddr_in *client_addr_ptr)
{
  // we must use a variable because accept() writes to it
  socklen_t sock_len = sizeof(*client_addr_ptr);

  if (debug)
    fprintf(stderr, "accepting a connection on fd %d\n", listen_socket);

  int new_socket_fd = accept(listen_socket,
                             (struct sockaddr *)client_addr_ptr,
                             &sock_len);
  if (new_socket_fd < 0)
  {
    perror("accept failed");
    return FAIL;
  }
  if (debug)
    fprintf(stderr, "Connection accepted. client fd is %d\n", new_socket_fd);

  return new_socket_fd;
}

int close_down_listening(int listening_socket)
{
  if (debug)
    fprintf(stderr, "closing socket fd %d\n", listening_socket);

  close(listening_socket);

  return !FAIL;
}

// returns FAIL for error, 1 for success
//! Currently no "time to quit" handling
int handle_new_client_wrapper(int socket_fd, struct sockaddr_in client_addr)
{
  pthread_t client_handler_thread;

  // we must allocate this because we (probably) return before thread executes
  Thread_data *client_info = malloc(sizeof(Thread_data));

  client_info->socket_fd = socket_fd;
  client_info->client_address = client_addr;

  int result = pthread_create(&client_handler_thread,
                              NULL, // Use default thread attributes
                              single_client_handler_threadfunc,
                              (void *)client_info);
  if (result < 0)
  {
    perror("pthread_create");
    return FAIL;
  }

  if (debug)
    fprintf(stderr, "Client handling thread is %lu\n", client_handler_thread);

  return !FAIL;
}

// Payload ptr will be freed in this handler
void *single_client_handler_threadfunc(void *payload_ptr)
{
  Thread_data client = *(Thread_data *)payload_ptr;
  free(payload_ptr);

  int result = handle_new_client_guts(client.socket_fd, client.client_address);

  if (debug)
    fprintf(stderr, "handle_new_client_guts (socket %d) returned %d\n",
            client.socket_fd,
            result);

  return NULL;
}

int handle_new_client_guts(int socket_fd, struct sockaddr_in client_addr)
{
  while (1)
  {
    char *request;
    int result = read_http_request(socket_fd, &request);

    if (result == FAIL)
    {
      fprintf(stderr, "socket %d read failed - closing, returning",
              socket_fd);
      close(socket_fd);
      return FAIL;
    }

    if (strlen(request) == 0)
    {
      fprintf(stderr,
              "socket %d - client closed socket - closing, returning\n",
              socket_fd);
      close(socket_fd);
      free(request);
      return !FAIL;
    }

    if (debug)
      fprintf(stderr, "client sent request (%d bytes): \n"
                      "---%s\n"
                      "---\n",
              result,
              request);

    result = respond_to_http_request(socket_fd, request);
    free(request);
    if (result == FAIL)
    {
      fprintf(stderr, "socket %d response failed - closing, returning",
              socket_fd);
      close(socket_fd);
      return FAIL;
    }
  }
}

// technically, we're just reading whatever they send us.
int read_http_request(int socket_fd, char **request_ptr)
{
  char incoming[MAX_MESSAGE_LENGTH - 1];

  int amount_read = read(socket_fd, incoming, sizeof(incoming));

  if (amount_read < 0)
  {
    perror("read_http_request");
    return FAIL;
  }

  incoming[amount_read] = '\0';

  *request_ptr = strdup(incoming);

  if (amount_read == 0)
  {
    // client side closed connection
    return !FAIL;
  }

  return !FAIL;
}

int send_http_response(int socket_fd, char *body)
{
  char response[MAX_MESSAGE_LENGTH];

  snprintf(response, sizeof(response),
           "HTTP/1.1 200\n"
           "Content-type: text/plain\n"
           "Content-Length: %d\n"
           "Connection: Keep-Alive\n"
           "\n%s",
           (int)strlen(body), body);

  int result = write(socket_fd, response, strlen(response));

  if (result == -1)
  {
    perror("write failed");
    return FAIL;
  }
  if (debug)
    fprintf(stderr, "wrote %d bytes to client socket fd %d\n",
            result,
            socket_fd);

  return !FAIL;
}

int send_error_response(int socket_fd)
{
  return send_http_response(socket_fd,
                            "Invalid request.\n"
                            "\n"
                            "\"/plus/#/#\" is the only valid path.\n");
}

int respond_to_http_request(int socket_fd, char *request)
{
  int num1;
  int num2;
  int result = sscanf(request, "GET /plus/%d/%d ", &num1, &num2);

  if (result < 2 || result == EOF)
  {
    send_error_response(socket_fd);
    return !FAIL;
  }

  char response_body[MAX_MESSAGE_LENGTH];
  snprintf(response_body, sizeof(response_body),
           "Sum of %d and %d is %d.\n",
           num1,
           num2,
           num1 + num2);

  return send_http_response(socket_fd, response_body);
}
