#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct Request {
    char method[MAXLINE];
    char uri[MAXLINE];
    char hostname[MAXLINE];
    char path[MAXLINE];
    char version[MAXLINE];
    struct RequestHeader *root;
} request;

typedef struct RequestHeader {
    char name[MAXLINE];
    char data[MAXLINE];
    struct RequestHeader *next_header;
} request_header;

typedef struct CacheLine {
    char hostname[MAXLINE];
    char path[MAXLINE];
    size_t size;
    char *data;
    int lru_counter;
    struct CacheLine *next_line; /* Use linked list */
} cache_line;


/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";
cache_line *root_line = NULL;
unsigned int cache_size = 0;
// request_header *root_header = NULL;

void *proxy_thread(void *vargp);
void send_request(request *request_line, int fd);
void parse_line(request *request_line, char *buf);
void parse_header(request *request_line, char *buf);
void create_header(request *request_line);
void add_header(request *request_line, request_header *header);
void destruct_request(request *request_line);
request_header* find_header(request *request_line, char *key);

void initialize_cache();
void insert_cache(char *buf, request *request_line);
void evict_cache();
void update_cache(cache_line *updated_line);
void destruct_cache();
cache_line *search_cache(char* hostname, char *path);

int main(int argc, char **argv)
{
    /* initialize eveything such as data structure */
    /* check port number */
    /* establish listening requests */
    /* when a client connects spawn a new thread to handle it */
    int listenfd;
    int *connfdp;
    pthread_t tid;
    char port[MAXLINE], hostname[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_in clientaddr;

    /* Check command line args */
    if (argc != 2) { // ./proxy portnum
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    Signal(SIGPIPE, SIG_IGN);   /* Ignore SIGPIPE */

    initialize_cache();

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        //printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, proxy_thread, connfdp);
    }   
    destruct_cache();
    return 0;
}

void *proxy_thread(void *vargp) 
{
    int connfd = *((int *) vargp); /* client fd */
    Free(vargp);

    Pthread_detach(pthread_self());

    request *request_line = Malloc(sizeof(request));
    request_line->root = NULL;
    rio_t rio_client;
    char buf[MAXLINE];

    Rio_readinitb(&rio_client, connfd);
    Rio_readlineb(&rio_client, buf, MAXLINE);
    parse_line(request_line, buf);
    memset(&buf[0], 0, sizeof(buf));

    Rio_readlineb(&rio_client, buf, MAXLINE);
    while (strcmp(buf, "\r\n")) {
        parse_header(request_line, buf);
        memset(&buf[0], 0, sizeof(buf));
        Rio_readlineb(&rio_client, buf, MAXLINE);
    }
    memset(&buf[0], 0, sizeof(buf));
    create_header(request_line);
    send_request(request_line, connfd);
    //destruct_request(request_line);
    printf("finished - ending thread\n");
    return NULL;
}

/*
 * parse_line : parse "GET /index.html HTTP/1.0"
 */
void parse_line(request *request_line, char *buf) 
{
    char method[MAXBUF];
    char uri[MAXBUF];
    char version[MAXBUF];
    char *hostname;
    char *path;

    sscanf(buf, "%s %s %s\n", method, uri, version);
    strcpy(request_line->method, method);
    strcpy(request_line->uri, uri);
    strcpy(request_line->version, version);

    /* http://cs.cmu.edu/index.html */
    hostname = strstr(uri, "http://");

    if (!hostname)
        return;
    hostname += 7; /* Remove the http:// part */
    
    if (!(path = strstr(hostname, "/"))) {
        strcpy(request_line->hostname, hostname);
        strcpy(request_line->path, "/");
    } else {
        request_line->hostname[0] = '\0';
        strncat(request_line->hostname, hostname, path - hostname);
        strcpy(request_line->path, path);
    }

    if(strcmp("GET", method) != 0)
        printf("Unsupported method error : %s\n", method);
}

void send_request(request *request_line, int connfd)
{
    printf("send_request called\n");
    char* port;
    char request_port[100];
    char request_domain[200];
    char request_buf[MAXLINE];
    request_header* header;
    int requestfd;
    int i;

    if (strlen(request_line->hostname))
        strcpy(request_domain, request_line->hostname);
    else if (header = find_header(request_line, "Host"))
        strcpy(request_domain, header->data);
    else {
        printf("error occur: host not found\n");
        return;
    }

    if ((port = strstr(request_domain, ":"))) {
        port++;
        strcpy(request_port, port);
    } else
        strcpy(request_port, "80");

    i = sprintf(request_buf, "%s %s HTTP/1.0\r\n", request_line->method, request_line->path);
    header = request_line->root;

    while (header) {
        i += sprintf(request_buf + i, "%s: %s\r\n", header->name, header->data);
        header = header->next_header;
    }

    printf("%s\r\n", request_buf);

    request_header *host = find_header(request_line, "Host");
    cache_line *target = search_cache(host->data, request_line->path);
    if (target) {
        Rio_writen(connfd, target->data, target->size);
        update_cache(target);
        Close(connfd);
        return;
    }

    requestfd = Open_clientfd(request_domain, request_port);
    Rio_writen(requestfd, request_buf, strlen(request_buf));

    rio_t rio;
    char response_buf[MAXLINE];
    char cache_buf[MAX_OBJECT_SIZE];
    char *cache_ptr = cache_buf;
    int IS_CACHEPOSSIBLE = 1;
    ssize_t len = 0;

    Rio_readinitb(&rio, requestfd);
    while((len = Rio_readnb(&rio, response_buf, MAXLINE)) > 0) {
        Rio_writen(connfd, response_buf, (size_t) len);
        if (IS_CACHEPOSSIBLE) {
            if ((len + (cache_ptr - cache_buf)) <= MAX_OBJECT_SIZE) {
                cache_ptr[0] = '\0';
                strncat(cache_ptr, response_buf, len);
                cache_ptr += len;
            } else
                IS_CACHEPOSSIBLE = 0;
        }
    }
    if (IS_CACHEPOSSIBLE)
        insert_cache(response_buf, request_line);
    Close(requestfd);
    Close(connfd);

    puts(response_buf);
}

/* Functions to Manipulate Header Linked-List */
void parse_header(request *request_line, char *buf)
{
    //printf("parse_header called\n");
    /* Host: cs.cmu.edu */
    request_header *header = Malloc(sizeof(request_header));

    char *name = strstr(buf, ": ");
    char *data = strstr(buf, "\r\n");

    if ((!name) || (!data)) {
        printf("Error: The Header Format is wrong\n");
        return;
    }

    header->name[0] = '\0';
    header->data[0] = '\0';
    strncat(header->name, buf, name - buf);
    strncat(header->data, name + 2, data - name - 2);

    add_header(request_line, header);
}

void add_header(request *request_line, request_header *header)
{
    printf("add_header called\n");
    request_header *last = NULL;

    if (!(request_line->root)) {
        request_line->root = header;
        header->next_header = NULL;
    } else {
        last = request_line->root;

        while (last && last->next_header)
            last = last->next_header;
        last->next_header = header;
        header->next_header = NULL;
    }
}

void create_header(request *request_line)
{
    printf("create_header called\n");
    if (!find_header(request_line, "Host")) {
        request_header *header = Malloc(sizeof(request_header));
        strcpy(header->name, "Host");
        strcpy(header->data, request_line->hostname);
        add_header(request_line, header);
    }

    request_header *header;
    if (!(header = find_header(request_line, "User-Agent"))) {
        request_header *header = Malloc(sizeof(request_header));
        strcpy(header->name, "User-Agent");
        strcpy(header->data, user_agent_hdr);
        add_header(request_line, header);
    } else {
        strcpy(header->name, "User-Agent");
        strcpy(header->data, user_agent_hdr);
    }

    if (!find_header(request_line, "Connection")) {
        request_header *header = Malloc(sizeof(request_header));
        strcpy(header->name, "Connection");
        strcpy(header->data, "close");
        add_header(request_line, header);
    } else {
        strcpy(header->name, "Connection");
        strcpy(header->data, "close");
    }

    if (!(header = find_header(request_line, "Proxy-Connection"))) {
        header = Malloc(sizeof(request_header));
        strcpy(header->name, "Proxy-Connection");
        strcpy(header->data, "close");
        add_header(request_line, header);
    } else {
        strcpy(header->name, "Proxy-Connection");
        strcpy(header->data, "close");
    }
}

void destruct_request(request *request_line)
{
    printf("destruct_request called\n");
    request_header *target = request_line->root;
    request_header *next = target;

    while(next) {
        next = target->next_header;
        Free(target);
        target = next;
    }
    Free(request_line);
}

/* Helper Functions */
request_header *find_header(request *request_line, char *key)
{
    printf("find_header called\n");
    request_header *target;
    target = request_line->root;

    while (target) {
        if (!(strcmp(target->name, key)))
            return target;
        else    
            target = target->next_header;
    }

    /* If the key doesn't exist!*/
    return NULL;
}

/* Cache Functions */
void initialize_cache()
{
    printf("initialize_cache called\n");
    root_line = Malloc(sizeof(cache_line));

    strcpy(root_line->hostname, "");
    strcpy(root_line->path, "");
    root_line->size = 0;
    root_line->data = NULL;
    root_line->lru_counter = 0;
    root_line->next_line = NULL;
    //printf("initialize_cache success\n");
}

void insert_cache(char *buf, request *request_line)
{
    printf("insert_cache called\n");
    /* check size */
    size_t candidate_size = strlen(buf);
    
    if (candidate_size > MAX_OBJECT_SIZE)
        return;
    if (cache_size + candidate_size > MAX_CACHE_SIZE)
        while (cache_size + candidate_size <= MAX_CACHE_SIZE)
            evict_cache();

    cache_line *new_line = Malloc(sizeof(cache_line));
    cache_line *last_line = root_line;
    while(last_line->next_line)
        last_line = last_line->next_line;
    last_line->next_line = new_line;
    new_line->next_line = NULL;

    strcpy(new_line->hostname, request_line->hostname);
    strcpy(new_line->path, request_line->path);
    new_line->data = Malloc(candidate_size);
    new_line->data[0] = '\0';
    strncat(new_line->data, buf, candidate_size);
    strcpy(new_line->data, buf);
    new_line->size = candidate_size;
    new_line->lru_counter = 0;

    cache_size += candidate_size;
    update_cache(new_line);
}

void update_cache(cache_line *updated_line)
{
    printf("update cache called\n");
    cache_line *target_line = root_line;
    while(target_line->next_line) {
        target_line = target_line->next_line;
        if (target_line != updated_line)
            target_line->lru_counter++;
        else
            target_line->lru_counter = 0;
    }
    printf("cache successfully updated\n");
}

void evict_cache()
{
    printf("evict_cache called\n");
    int max = 0;
    cache_line *lru_line;

    cache_line *target_line = root_line;
    while(target_line->next_line) {
        target_line = target_line->next_line;
        if (target_line->lru_counter > max) {
            lru_line = target_line;
            max = target_line->lru_counter;
        }
    }

    target_line = root_line;
    while((target_line->next_line != lru_line) && target_line)
        target_line = target_line->next_line;
    target_line->next_line = lru_line->next_line;
    cache_size -= lru_line->size;
    Free(lru_line->data);
    Free(lru_line);
}

void destruct_cache()
{
    printf("destruct cache\n");
    cache_line *target = root_line;
    cache_line *next = target;
    cache_size = 0;

    while(next) {
        next = target->next_line;
        // cache_size -= target->size;
        Free(target->data);
        Free(target);
    }
}

cache_line *search_cache(char *hostname, char *path)
{
    printf("search_cache called\n");
    cache_line *target = root_line;
    while (target) {
        if ((strcmp(hostname, target->hostname)) && (strcmp(path, target->path)))
            return target;
        target = target->next_line;
    }
    return NULL;
}





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
