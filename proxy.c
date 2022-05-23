/* 

Part 1 : Implementing a sequential web proxy

basic HTTP operation & socket programming
- set up the proxy to accept incoming connections
- read and parse requests
- forward request to web servers
- read the servers' responses
- forward those responses to the corresponding clients

Part 2 : Dealing with mulitple concurrent requests

- upgrade your proxy to deal with multiple concurrent connections
- multi-threading

Part 3 : Caching web objects
- add caching to your proxy using a simple main memory cache of recently accessed web content
- cache individual objects, not the whole page
- use a LRU eviction policy
- your caching system must allow for concurrent reads while maintaining consistency

*/


#include <string.h>
#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct
{
	char method[MAXLINE];
	char uri[MAXLINE];
	char hostname[MAXLINE];
	char port[MAXLINE];
	char path[MAXLINE];
	char version[MAXLINE];
	struct request_header *root;
} request_line;

typedef struct request_header
{
	char name[MAXLINE];
	char data[MAXLINE];
	struct request_header* next_header;
} request_header;

typedef struct cache_line
{
	char hostname[MAXLINE];
	char path[MAXLINE];
	unsigned long size;
	char *data;
	int lru_counter;
	struct cache_line* next_line;
} cache_line;

static const char *user_agent_hdr = "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";
cache_line* cache_root;
size_t cache_size = 0;

void *run_thread(void*);

void parse_request(request_line*, char*);
void send_request(int, request_line*);

void modify_header(request_line*);
void parse_header(request_line*, char*);
// void create_header(request_line*, char*);
void insert_header(request_line*, request_header*);
request_header *search_header(request_line*, char*);

void initialize_cache();
// void insert_cache(request_line*, char*, size_t);
void update_cache(cache_line*);
void destruct_cache();
void evict_cache();
cache_line *create_cache();
cache_line *search_cache(char*, char*);


int main(int argc, char **argv) 
{
	/* initialize eveything such as data structure */
	/* check port number */
	/* establish listening requests */
	/* when a client connects spawn a new thread to handle it */
	struct sockaddr_in clientaddr;
	pthread_t tid;

	int listenfd, *connfd;
	unsigned int clientlen;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	Signal(SIGPIPE, SIG_IGN);   /* Ignore SIGPIPE */
	
	initialize_cache();			/* Intialize cache (linked list) */

	listenfd = Open_listenfd(argv[1]); //Open_listenfd => socket(), bind(), listen()
	while (1) {
		clientlen = sizeof(clientaddr);
		connfd = Malloc(sizeof(int));
		*connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen); //typedef struct sockaddr SA;

		// char port[MAXLINE], hostname[MAXLINE];
		// Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		// printf("Accepted connection from (%s, %s)\n", hostname, port);

		Pthread_create(&tid, NULL, run_thread, connfd);
	}
	destruct_cache();
	return 0;
}

void *run_thread(void* vargp)
{
	rio_t rio;
	char buf[MAXLINE];

	int connfd = *((int*)vargp);
	free(vargp);
	Pthread_detach(pthread_self());

	request_line *request = Malloc(sizeof(request_line));
	request->root = NULL;

	Rio_readinitb(&rio, connfd);
	Rio_readlineb(&rio, buf, MAXLINE);
	parse_request(request, buf);
	memset(&buf[0], 0, sizeof(buf));

	Rio_readlineb(&rio, buf, MAXLINE);
	while(strcmp(buf, "\r\n")) {
		parse_header(request, buf);
		memset(&buf[0], 0, sizeof(buf));
		Rio_readlineb(&rio, buf, MAXLINE);
	}
	memset(&buf[0], 0, sizeof(buf));
	modify_header(request);
	send_request(connfd, request);
	return NULL;
}

/* Parsing Functions */
void parse_request(request_line *request, char* buf)
{
	char method[10];
	char uri[300];
	char version[100];
	char *host_start;
	char *path_start;
	char *port_start;
	sscanf(buf, "%s %s %s\n", method, uri, version);
	strcpy(request->method, method);
	strcpy(request->uri, uri);
	strcpy(request->version, version);

	// URL to hostname && path
	host_start = strstr(uri, "http://");
	if(!host_start) {
		//no hostname
		return;
	}
	host_start += 7;
	path_start = strstr(host_start, "/");
	if(!path_start) {
		//no path -> add default path '/'
		strcpy(request->hostname, host_start);
		strcpy(request->path, "/");
	} else {
		request->hostname[0] = '\0';
		strncat(request->hostname, host_start, path_start - host_start);
		strcpy(request->path, path_start);
	}

	port_start = strstr(request->hostname, ":");
	if (port_start) {
		*port_start = '\0';
		port_start++;
		strcpy(request->port, port_start);
  	} else strcpy(request->port, "80");

	if (strcmp("GET", method) !=0)
		printf("Only GET method can be accepted\n");
}

void parse_header(request_line *request, char *buf)
{
	request_header* header = Malloc(sizeof(request_header));
	char* pname= strstr(buf, ": ");
	char* pdata= strstr(buf, "\r\n");
	if((!pname)||(!pdata)) {
		printf("Error: Header format\n");
		return;
	}
	header->name[0] = '\0';
	header->data[0] = '\0';
	strncat(header->name, buf, pname - buf);
	strncat(header->data, pname + 2, pdata - pname - 2);
	insert_header(request, header);
}

void create_request(request_line *request, char *request_buf)
{
	request_header* header;

	strcat(request_buf, request->method);
	strcat(request_buf, " ");
	strcat(request_buf, request->path);
	strcat(request_buf, " ");
	strcat(request_buf, "HTTP/1.0\r\n");
	header = request->root;

	while (header) {
		strcat(request_buf, header->name);
		strcat(request_buf, ": ");
		strcat(request_buf, header->data);
		strcat(request_buf, "\r\n");
		header = header->next_header;
	}
	strcat(request_buf, "\r\n");

	printf("%s\r\n", request_buf);
}

/* I/O Functions */
void send_request(int connfd, request_line *request)
{
	char request_buf[MAXLINE * 2];
	char response_buf[MAXLINE];
	char cache_candidate[MAX_OBJECT_SIZE];
	char *cache_ptr = cache_candidate;
	
	rio_t rio;
	ssize_t len=0;
	int cachable = 1;
	int requestfd;

	create_request(request, request_buf);
	
	cache_line* target= search_cache(request->path, request->hostname);
	if (target) {
		Rio_writen(connfd, target->data, target->size);
		update_cache(target);
		Close(connfd);
		return;
  	}

	//open request file descriptor
	requestfd = Open_clientfd(request->hostname, request->port);

	//send request
	Rio_writen(requestfd, request_buf, strlen(request_buf));
	
	//recieve response
	Rio_readinitb(&rio, requestfd);
	while((len = Rio_readnb(&rio, response_buf, MAXLINE)) > 0)
	{
		Rio_writen(connfd, response_buf, (size_t) len);
		if (cachable) {
			if ((len + (cache_ptr - cache_candidate)) <= MAX_OBJECT_SIZE) {
				cache_ptr[0] = '\0';
				strncat(cache_ptr, response_buf, len); 
				cache_ptr += len;
			} else cachable=0;
		}
  	}
	if (cachable) {
		int size = cache_ptr - cache_candidate;
		if ((cache_size + size) >  MAX_CACHE_SIZE)
			while ((cache_size + size) > MAX_CACHE_SIZE)
				evict_cache();
		
		cache_line *new_line = create_cache();

		strcpy(new_line->hostname, request->hostname);
		strcpy(new_line->path, request->path);
		new_line->size = size;
		new_line->data = Malloc(size);
		new_line->data[0] = '\0';
		strncat(new_line->data, cache_candidate, size);
		
		cache_size += size;
		update_cache(new_line);
	}

	Close(requestfd);
	Close(connfd);
}

/* Header Manipulation Functions */
void modify_header(request_line* line)
{
	request_header *find = NULL;

	find = search_header(line, "Host");
	if(!find){
		request_header* new_header = Malloc(sizeof(request_header));
		strcpy(new_header->name, "Host");
		strcpy(new_header->data, line->hostname);
		insert_header(line, new_header);
	}
	find = NULL;

	find= search_header(line, "User-Agent");
	if(!find){
		request_header* new_header = Malloc(sizeof(request_header));
		strcpy(new_header->name, "User-Agent");
		strcpy(new_header->data, user_agent_hdr);
		insert_header(line, new_header);
	} else {
		strcpy(find->name, "User-Agent");
		strcpy(find->data, user_agent_hdr);
	}
	find = NULL;

	find= search_header(line, "Connection");
	if (!find) {
		request_header* new_header = Malloc(sizeof(request_header));
		strcpy(new_header->name, "Connection");
		strcpy(new_header->data, "close");
		insert_header(line, new_header);
	} else {
		strcpy(find->name, "Connection");
		strcpy(find->data, "close");
	}
	find = NULL;

	find= search_header(line, "Proxy-Connection");
	if (!find) {
		request_header* new_header = Malloc(sizeof(request_header));
		strcpy(new_header->name, "Proxy-Connection");
		strcpy(new_header->data, "close");
		insert_header(line, new_header);
	} else {
		strcpy(find->name, "Proxy-Connection");
		strcpy(find->data, "close");
	}
	find = NULL;
}

request_header *search_header(request_line *request, char* type)
{
	request_header *temp; 
	temp = request->root;
	while (temp) {
		if (strcmp(temp->name, type) == 0) return temp;
		else temp = temp->next_header;
	}
	return NULL;
}

void insert_header(request_line *request, request_header *header) 
{
	request_header *last = NULL;
	if (!(request->root)) {	/* If root doesn't exist, make header the root */
		request->root = header;
		header->next_header = NULL;
	} else {				/* If root exists, add header to the end of the linked list */
		last = request->root;
		while(last && last->next_header)
			last = last->next_header;
		last->next_header = header;
		header->next_header = NULL;
	}
}

/* Cache Related Functions */
void initialize_cache() 
{
	printf("initializing cache\n");
	cache_root = Malloc(sizeof(cache_line));

	strcpy(cache_root->hostname, "");
	strcpy(cache_root->path, "");
	cache_root->size = 0;
	cache_root->data = NULL;
	cache_root->next_line = NULL;
	cache_root->lru_counter = 0;
}

cache_line *create_cache()
{
	printf("creating a new cache item\n");
	cache_line *temp = cache_root;
	cache_line *new_line = Malloc(sizeof(cache_line));

	while (temp->next_line)
		temp = temp->next_line;
	temp->next_line = new_line;
	new_line->next_line = NULL;
	new_line->data = NULL;
	new_line->lru_counter = 0;
	
	return new_line;
} 

cache_line* search_cache(char *path, char *hostname)
{
	cache_line* temp = cache_root;
	while(temp != NULL) {
		if((strcmp(path, temp-> path) == 0) && (strcmp(hostname, temp-> hostname) == 0)) return temp;
		temp = temp->next_line;
	}
	return NULL;
}

void update_cache(cache_line* updated_line)
{
	cache_line *target_line = cache_root;
	while(target_line->next_line) {
		target_line = target_line->next_line;
		if (target_line != updated_line)
			target_line->lru_counter++;
		else
			target_line->lru_counter = 0;
	}
}

void evict_cache() {
	cache_line *temp = NULL;
	cache_line *target = NULL;
	int max = 0;

	temp = cache_root;
	temp = temp->next_line;
	target = temp;
	max = temp->lru_counter;
	while(temp){
		if(temp->lru_counter > max){
			max = temp->lru_counter;
			target = temp;
		}
		temp = temp->next_line;
	} 

	temp = cache_root;

	if(!target) return;
	if(!temp) return;
	while((temp->next_line != target) && temp)
		temp = temp->next_line;
	if(!temp) return;
	temp->next_line = target ->next_line;
	cache_size -= (target->size);
	Free(target->data);
	Free(target);
	return;
}
