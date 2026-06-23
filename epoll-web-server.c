#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#define RMAX 4095
#define HMAX 1024
#define BMAX 1024
#define MAXFDS 10000

static char request[RMAX+1];

static int HSIZE = 0;
static char header[HMAX];

static int BSIZE = 0;
static char body[BMAX];

static char saved_data[BMAX];
static int saved_size = 0;

struct client_state {
    int  in_use;
    int  filefd;
    off_t file_remaining;
    char buf[1024];
    int  buf_len;
    int  buf_sent;
};

static struct client_state clients[MAXFDS];
static int efd;

static char *
find_header_end(char *buf)
{
	return strstr(buf, "\r\n\r\n");
}

static int
parse_content_length(char *headers_start, char *headers_end)
{
	char *p = headers_start;

	while(p < headers_end){
		char *line_end = memchr(p, '\n', headers_end - p);
		if(line_end == NULL || line_end > headers_end){
			line_end = headers_end;
		}

		if(strncmp(p, "Content-Length:", 15) == 0){
			char *q = p +15;
			while(q < line_end && (*q == ' ' || *q == '\t')){
				q++;
			}
			return atoi(q);
		}

		if(line_end == headers_end){
			break;
		}
		p = line_end + 1;
	}
	return -1;
}

static int
open_listenfd(int port)
{
   int listenfd = socket(AF_INET, SOCK_STREAM, 0);

   int optval = 1;
   setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

   struct sockaddr_in server;
   server.sin_family = AF_INET;
   server.sin_port = htons(port);
   inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);
   bind(listenfd, (struct sockaddr*)&server, sizeof(server));
   listen(listenfd, 10);
   return listenfd;
}

static int
accept_connection(int listenfd)
{
	struct sockaddr_in client;
	socklen_t csize = sizeof(client);
	memset(&client, 0x00, sizeof(client));
	return accept(listenfd, (struct sockaddr*)&client, &csize);
}

void send_data(int clientfd, char buf[], int size)
{
	ssize_t amt;
	int total = 0;

	while(total < size){
		amt = send(clientfd, buf + total, size - total, 0);
		if(amt <= 0){
			break;
		}
		total += (int)amt;
	}
}

static void send_response(int clientfd)
{
	send_data(clientfd, header, HSIZE);
	send_data(clientfd, body, BSIZE);
}

static void
send_error(int clientfd, int code)
{
	if(code == 400){
		HSIZE = snprintf(header, HMAX,
				"HTTP/1.1 400 Bad Request\r\n"
				"\r\n");
		BSIZE = 0;
	} else if(code == 413){
		HSIZE = snprintf(header, HMAX,
				"HTTP/1.1 413 Request Entity Too Large\r\n"
				"\r\n");
		BSIZE = 0;
	} else if (code == 404) {
	       HSIZE = snprintf(header, HMAX,
		       "HTTP/1.1 404 Not Found\r\n"
		       "\r\n");
	       BSIZE = 0;
	}else{
		HSIZE = snprintf(header, HMAX,
				"HTTP/1.1 400 Bad Request\r\n"
				"\r\n");
		BSIZE = 0;
	}
	send_response(clientfd);
}

static void
handle_status(int clientfd)
{
	const char *msg = "alive";
	BSIZE = 5;
	memcpy(body, msg, BSIZE);

	HSIZE = snprintf(header, HMAX,
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: %d\r\n"
			"\r\n",
			BSIZE);
	
	send_response(clientfd);
}

static void
handle_echo(int clientfd, char *req, int req_len)
{
        char *first_crlf = strstr(req, "\r\n");
        if(first_crlf == NULL){
                send_error(clientfd, 400);
		return;
        }

        char *headers_start = first_crlf +2;

        char *headers_end = find_header_end(req);
        if(headers_end == NULL || headers_end <= headers_start) {
                send_error(clientfd, 400);
		return;
        }

        int len = (int)(headers_end - headers_start);
        if(len > BMAX) {
                send_error(clientfd, 413);
		return;
        }

        BSIZE = len;
        memcpy(body, headers_start, BSIZE);
        HSIZE = snprintf(header, HMAX,
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Length: %d\r\n"
                        "\r\n",
                        BSIZE);

        send_response(clientfd);
}

static void
handle_post_info(int clientfd, char *req, int req_len)
{
	char *first_crlf = strstr(req, "\r\n");
	if(first_crlf == NULL){
		send_error(clientfd, 400);
		return;
	}

	char *headers_start = first_crlf + 2;
	char *headers_end = find_header_end(req);
	
	if(headers_end == NULL || headers_end <= headers_start){
		send_error(clientfd, 400);
		return;
	}

	int content_length = parse_content_length(headers_start, headers_end);
	if(content_length < 0) {
		send_error(clientfd, 400);
		return;
	}

	if(content_length > BMAX){
		send_error(clientfd, 413);
		return;
	}

	char *body_start = headers_end + 4;
	int available = req_len - (int)(body_start - req);
	if(available < 0){
		available = 0;
	}

	int to_copy = content_length;
	if(available < content_length){
		to_copy = available;
	}
	if(to_copy > BMAX){
		to_copy = BMAX;
	}

	memcpy(saved_data, body_start, to_copy);
	saved_size = to_copy;
	BSIZE = to_copy;
	memcpy(body, saved_data, BSIZE);

	HSIZE = snprintf(header, HMAX,
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: %d\r\n"
			"\r\n",
			BSIZE);

	send_response(clientfd);
}

static void
handle_saved(int clientfd)
{
	if(saved_size == 0){
		const char *empty = "!!EMPTY!!";
		BSIZE = 9;
		memcpy(body, empty, BSIZE);
	}else{
		BSIZE = saved_size;
		memcpy(body, saved_data, BSIZE);
	}

	HSIZE = snprintf(header, HMAX,
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: %d\r\n"
			"\r\n",
			BSIZE);

	send_response(clientfd);
}

static int
send_file_chunk(int clientfd)
{
	if(clientfd < 0 || clientfd >= MAXFDS)
		return -1;

	struct client_state *st = &clients[clientfd];
	if(!st->in_use || st->filefd < 0)
		return -1;

	if(st->buf_sent < st->buf_len){
		ssize_t to_send = st->buf_len - st->buf_sent; 
		ssize_t sent = send(clientfd, st->buf + st->buf_sent, to_send, 0);
		if(sent < 0){
			if(errno == EAGAIN || errno == EWOULDBLOCK){
				return 0;
			}
			return -1;
		}
		if(sent == 0){
			return -1;
		}
		st->buf_sent += (int)sent;

		if(st->buf_sent < st->buf_len){
			return 0;
		}
	}
	
	if(st->file_remaining == 0){
		close(st->filefd);
		st->filefd = -1;
		st->in_use = 0;
		return 1;
	}

	ssize_t to_read = sizeof(st->buf);
	if(st->file_remaining < to_read) {
		to_read = st->file_remaining;
	}

	ssize_t n = read(st->filefd, st->buf, to_read);
	if(n <= 0) {
		close(st->filefd);
		st->filefd = -1;
		st->in_use = 0;
		return -1;
	}

	st->buf_len = (int)n;
	st->buf_sent = 0;
	st->file_remaining -= n;

	ssize_t sent = send(clientfd, st->buf, st->buf_len, 0);
	if(sent < 0){
		if(errno == EAGAIN || errno == EWOULDBLOCK){
			return 0;
		}
		return -1;
	}
	if(sent == 0){
		return -1;
	}

	st->buf_sent = (int)sent;
	if(st->buf_sent < st->buf_len){
		return 0;
	}


	if(st->file_remaining == 0){
		close(st->filefd);
		st->filefd = -1;
		st->in_use = 0;
		return 1;
	}

	return 0;



    }

static int
handle_file_get(int clientfd, const char *uri)
{
	const char *path = uri;
	if(path[0] == '/'){
		path++;
	}

	if(path[0] == '/'){
		path++;
	}

	int fd = open(path, O_RDONLY);
	if(fd < 0){
		send_error(clientfd, 404);
		return 0;
	}

	struct stat st;

	if(fstat(fd, &st) == -1) {
		close(fd);
		send_error(clientfd, 404);
		return 0;
	}

	if(!S_ISREG(st.st_mode)){
		 close(fd);
		 send_error(clientfd, 404);
		 return 0;
	}

	off_t filesize = st.st_size;

	HSIZE = snprintf(header, HMAX,
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: %lld\r\n"
			"\r\n",
			(long long)filesize);

	send_data(clientfd, header, HSIZE);

	if(clientfd < 0 || clientfd >= MAXFDS){
		close(fd);
		return 0;
	}

	struct client_state *stc = &clients[clientfd];
	stc->in_use = 1;
	stc->filefd = fd;
	stc->file_remaining = filesize;
	stc->buf_len = 0;
	stc->buf_sent = 0;

	struct epoll_event ev;
	ev.events = EPOLLOUT;
	ev.data.fd = clientfd;
	if(epoll_ctl(efd, EPOLL_CTL_MOD, clientfd, &ev) < 0){
		close(fd);
		stc->in_use = 0;
		return 0;
	}
	return 1;
}

static int
handle_client(int clientfd)
{
	ssize_t r = recv(clientfd, request, RMAX, 0);
	if(r <= 0){
		return 0;
	}

	request[r] = '\0';

	char method[8];
	char uri[256];
	char version[16];

	if(sscanf(request, "%7s %255s %15s", method, uri, version) != 3){
		send_error(clientfd, 400);
		return 0;
	}

	char *headers_end = find_header_end(request);
	if(headers_end == NULL){
		send_error(clientfd, 400);
		return 0;
	}

	if(strcmp(method, "GET") == 0){
		if (strcmp(uri, "/status") == 0) {
			handle_status(clientfd);
			return 0;
		} else if (strcmp(uri, "/echo") == 0) {
			handle_echo(clientfd, request, (int)r);
			return 0;
		} else if (strcmp(uri, "/saved") == 0){
			handle_saved(clientfd);
			return 0;
		}else {
			int keep_open = handle_file_get(clientfd, uri);
			return keep_open;
		}
	}else if (strcmp(method, "POST") == 0){
		if(strcmp(uri, "/info") == 0){
			handle_post_info(clientfd, request, (int)r);
		}else{
			send_error(clientfd, 400);
		}
		return 0;
	} else {
		send_error(clientfd, 400);
		return 0;
	}

}


int
main(int argc, char * argv[])
{
    assert(argc == 2);
    int port = atoi(argv[1]);
    
    int listenfd = open_listenfd(port);

    efd = epoll_create1(0);
    assert(efd >= 0);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listenfd;
    epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &ev);

    struct epoll_event events[64];


    while (1) {
	    int n = epoll_wait(efd, events, 64, -1);
	    if(n < 0) {
		    if(errno == EINTR){
		    continue;
		    }
		    perror("epoll_wait");
		    continue;
	    }

	    for(int i = 0; i < n; i++){
		    int fd = events[i].data.fd;
		    uint32_t evmask = events[i].events;

		    if(fd == listenfd){
			    int clientfd = accept_connection(listenfd);
			    if(clientfd >=0){
				    struct epoll_event cev;
				    cev.events = EPOLLIN;
				    cev.data.fd = clientfd;
				    if(epoll_ctl(efd, EPOLL_CTL_ADD, clientfd, &cev) < 0){
					    close(clientfd);
					    continue;
				    }

				    if(clientfd < MAXFDS){
					    clients[clientfd].in_use = 0;
					    clients[clientfd].filefd = -1;
					    clients[clientfd].file_remaining = 0;
					    clients[clientfd].buf_len = 0;
					    clients[clientfd].buf_sent = 0;
				    }
			    }
		    }else{
			    if(evmask & EPOLLIN){
				    int keep_open = handle_client(fd);
				    if(!keep_open){
					    if(fd < MAXFDS && clients[fd].in_use){
						    if(clients[fd].filefd >= 0){
							    close(clients[fd].filefd);
						    }
						    clients[fd].in_use = 0;
					    }
					    epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
					    close(fd);
					    continue;
				    }
			    }
			    if(evmask & EPOLLOUT){
				    if(fd >= 0 && fd < MAXFDS && clients[fd].in_use){
					    int done = send_file_chunk(fd);
					    if(done != 0) {
						    epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
						    close(fd);
					    }
				    }else{
					    epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
					    close(fd);
				    }
			    }
		    }
	    }
			    
    }

    return EXIT_SUCCESS;
}
