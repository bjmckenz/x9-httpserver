// seeded from https://www.binarytides.com/socket-programming-c-linux-tutorial/

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

int debug = 1;

#define LISTEN_PORT 8888
#define PENDING_CONNECTIONS_QUEUE_LENGTH 3

// forward decls
#define FAIL 0
//! All return FAIL (0). Anything else is successey
int establish_listening_socket(int port_to_listen);
int handle_new_client(int socket_fd, struct sockaddr_in client_addr);
int accept_a_client(int listen_socket, struct sockaddr_in *client_addr_ptr);
int close_down_listening(int listening_socket);

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
      keep_going = handle_new_client(new_client_fd, new_client_address);
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
  if (bind(new_socket_fd, (struct sockaddr *)&our_address, sizeof(our_address)) < 0)
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

// returns FAIL for error, 1 for success
//! Currently no "time to quit" handling
int handle_new_client(int socket_fd, struct sockaddr_in client_addr)
{
  // Reply to the client
  const char *message = "Hello client, I have received your connection. "
                        "But I have to go now. "
                        "Bye\n";

  int result = write(socket_fd, message, strlen(message));

  if (result == -1)
  {
    perror("write failed");
    return FAIL;
  }
  if (debug)
    fprintf(stderr, "wrote %d bytes to client socket fd %d\n", result, socket_fd);

  if (debug)
    fprintf(stderr, "closing client socket fd %d\n", socket_fd);

  close(socket_fd);

  return !FAIL;
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
