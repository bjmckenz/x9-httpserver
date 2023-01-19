#include "Client.h"

int next_client_index = 1;

Client *client_new( int sock_fd, struct sockaddr_in *addr)
{
  Client *cl = malloc(sizeof(Client));
  cl->socket_fd = sock_fd;
  cl->address = *addr;
  cl->id = next_client_index++;

  return cl;
}

void client_free(Client* cl)
{
  if (cl->socket_fd != 0)
    close(cl->socket_fd);
  
  free(cl);
}

int client_socket(Client* cl)
{
  return cl->socket_fd;
}

struct sockaddr_in client_address(Client* cl)
{
  return cl->address;
}

int client_write(Client* cl, char* buffer)
{
  int result = write(cl->socket_fd, buffer, strlen(buffer));

  if (result == -1)
  {
    perror("write failed");
    return FAIL;
  }

  return SUCCESS;
}

int client_id(Client* cl)
{
  return cl->id;
}