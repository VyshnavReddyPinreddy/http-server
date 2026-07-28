#include<stdio.h>
#include<unistd.h>
#include<sys/socket.h>
#include<stdlib.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<string.h>
#include<sys/time.h>
#include<errno.h>
#include <limits.h> // Gives PATH_MAX
// the below three headers are for sendfile() feature
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include<time.h>

#include <sys/epoll.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_HEADERS 32
#define DOCUMENT_ROOT "www"

#define MAX_EVENTS 64

// for keep-alive

#define MAX_CLIENTS 1024
#define KEEP_ALIVE_TIMEOUT 5

typedef struct {
    int fd;
    char ip[INET_ADDRSTRLEN];
    uint16_t port;
    time_t last_activity;
} Client;

Client clients[MAX_CLIENTS];

typedef struct{
    char key[64];
    char value[256];
} Header;

typedef struct {
    char method[8];
    char path[256];
    char version[16];

    Header headers[MAX_HEADERS];
    int header_count;

} HttpRequest;

typedef struct {
    int status_code;
    size_t bytes_sent;
} ResponseInfo;

int create_server_socket(){
    // Create Socket
    int server_fd = socket(AF_INET,SOCK_STREAM,0);
    if(server_fd<0){
        perror("Socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    
    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // To be able to reuse the port 8080 frequently
    int opt = 1;
    setsockopt(server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt));

    // Bind Socket
    if(bind(server_fd,(struct sockaddr*)&server_addr,sizeof(server_addr))<0){
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // What for is 5 ? 
    if(listen(server_fd,5)<0){
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
	printf("Server listening on port %d...\n",PORT);
    printf("\n");
    return server_fd;
}

void set_nonblocking(int fd){
    int flags = fcntl(fd,F_GETFL,0);
    if(flags==-1){
        perror("fcntl(F_GETFL)");
        exit(EXIT_FAILURE);
    }

    if(fcntl(fd,F_SETFL,flags | O_NONBLOCK)==-1){
        perror("fcntl(F_SETFL)");
        exit(EXIT_FAILURE);
    }
}


int should_keep_alive(HttpRequest *request){
    for(int i=0;i<request->header_count;i++){
        if(strcasecmp(request->headers[i].key,"Connection")==0){
            if(strcasecmp(request->headers[i].value,"keep-alive")==0){
                return 1;
            }
            if(strcasecmp(request->headers[i].value,"close")==0){
                return 0;
            }
        }
    }

    // HTTP 1.1 defaults to keep-alive
    if(strcmp(request->version,"HTTP/1.1")==0){
        return 1;
    }
    return 0;
}

int parse_http_request(char *buffer,HttpRequest *request){
    request->header_count=0;
    char *saveptr;
    char *line = strtok_r(buffer, "\r\n", &saveptr);

    if(line == NULL) return -1;

    if(sscanf(line,"%7s %255s %15s",request->method,request->path,request->version)!=3){
        return -1;
    }

    while((line=strtok_r(NULL,"\r\n",&saveptr))!=NULL){
        if(strlen(line)==0) break;

        Header *header = &request->headers[request->header_count];

        if(sscanf(line,"%63[^:]: %255[^\n]",header->key,header->value)==2){
            request->header_count++;
        }

        if(request->header_count>=MAX_HEADERS){
            break;
        }
    }
    return 0;
}

void send_response(int client_fd,const char *status,const char *content_type,const void *body,size_t body_length,int keep_alive,int send_body){
    char header[1024];
    int header_length = snprintf(header,
                                sizeof(header),
                                "HTTP/1.1 %s\r\n"
                                "Content-type: %s\r\n"
                                "Content-Length: %zu\r\n"
                                "Connection: %s\r\n"
                                "\r\n",
                                status,
                                content_type,
                                body_length,
                                keep_alive ? "keep-alive" : "close"
                                );
    
    if(send(client_fd,header,header_length,0)<0){
        perror("send");
        return;
    }         
    // For HEAD method                  
    if(send_body && body_length>0){  
        if(send(client_fd,body,body_length,0)<0){
            perror("send");
        }
    }
}

int read_file(const char *filename,void **buffer,size_t *file_size){
    FILE *fp = fopen(filename,"rb");
    if(fp==NULL){
        return -1;
    }

    fseek(fp,0,SEEK_END); // Move to eof
    *file_size=ftell(fp);
    rewind(fp); // Go back to beginning

    *buffer = malloc(*file_size);
    if(*buffer==NULL){
        fclose(fp);
        return -1;
    }

    size_t bytes_read = fread(*buffer,1,*file_size,fp);
    fclose(fp);

    if(bytes_read != *file_size){
        free(*buffer);
        return -1;
    }
    
    return 0;
}

size_t serve_file(int client_fd,const char*filename,const char *content_type,int keep_alive,int send_body){
    int fd = open(filename,O_RDONLY);
    if(fd<0){
        perror("open");
        return 0;
    }
    
    struct stat st;
    if(fstat(fd,&st)<0){
        perror("fstat");
        close(fd);
        return 0;
    }

    off_t file_size = st.st_size;

    // Send headers only
    send_response(client_fd,"200 OK",content_type,NULL,file_size,keep_alive,0);

    if(!send_body){
        close(fd);
        return file_size;
    }

    off_t offset = 0;

    while(offset<file_size){
        ssize_t sent = sendfile(client_fd,fd,&offset,file_size-offset);
        if(sent<=0){
            perror("sendfile");
            break;
        }
    }

    close(fd);
    return (size_t)file_size;
}

const char *get_mime_type(const char *filename){
    char *ext = strrchr(filename,'.');

    if(ext == NULL)
        return "application/octet-stream";

    if(strcmp(ext,".html")==0)
        return "text/html";

    if(strcmp(ext,".css")==0)
        return "text/css";

    if(strcmp(ext,".js")==0)
        return "application/javascript";

    if(strcmp(ext,".png")==0)
        return "image/png";

    if(strcmp(ext,".jpg")==0)
        return "image/jpeg";

    if(strcmp(ext,".jpeg")==0)
        return "image/jpeg";

    if(strcmp(ext,".gif")==0)
        return "image/gif";

    if(strcmp(ext,".ico")==0)
        return "image/x-icon";

    return "application/octet-stream";
}

void cleanup_idle_clients(int epoll_fd){
    time_t now = time(NULL);
    for (int i=0;i<MAX_CLIENTS;i++){
        if (clients[i].fd == -1) continue;

        if(now-clients[i].last_activity>=KEEP_ALIVE_TIMEOUT){
                printf("Closing idle client %s:%u\n",
                                                    clients[i].ip,
                                                    clients[i].port);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, i, NULL);
            close(i);
            clients[i].fd = -1;
        }
    }
}

int resolve_path(const char*request_path,char *resolved_path){
    char root[PATH_MAX];
    char requested[PATH_MAX];

    if(realpath(DOCUMENT_ROOT,root)==NULL){
        perror("realpath(root)");
        return -1;
    }

    // Build Requested path
    snprintf(requested,sizeof(requested),"%s%s",DOCUMENT_ROOT,request_path);

    // Canonicalize it 
    if(realpath(requested,resolved_path)==NULL){
        // File doesn't exist (or cannot be resolved)
        return -1;
    }

    // is resolved path in the document root ? 
    if(strncmp(root,resolved_path,strlen(root))!=0){
        return -2;
    }
    return 0;
}

ResponseInfo route_request(int client_fd,HttpRequest *request,int keep_alive){
    ResponseInfo res;

    char filename[PATH_MAX];

    const char *path = (strcmp(request->path,"/")==0) ? "/index.html" : request->path;

    int send_body = 1;
    if(strcmp(request->method,"HEAD")==0) send_body=0;

    if(strcmp(request->method,"GET")!=0 && strcmp(request->method,"HEAD")!=0){
        const char * msg = "405 Method not allowed";
        res.status_code = 405;
        res.bytes_sent = strlen(msg);

        send_response(client_fd,
                    "405 Method not allowed",
                    "text/plain",
                    msg,
                    strlen(msg),
                    keep_alive,
                    1);
        return res;
    }

    int status = resolve_path(path,filename);
    if(status==-2){
        const char *msg = "403 Forbidden";
        res.status_code = 403;
        res.bytes_sent = strlen(msg);
        send_response(client_fd,
                    "403 Forbidden",
                    "text/plain",
                    msg,
                    strlen(msg),
                    keep_alive,send_body);
        return res;
    }
    if(status==-1){
        const char *msg = "404 Not Found";
        res.status_code = 404;
        res.bytes_sent = strlen(msg);
        send_response(client_fd,
                    "404 Not Found",
                    "text/plain",
                    msg,
                    strlen(msg),
                    keep_alive,send_body);
        return res;
    }   

    res.status_code = 200;
    res.bytes_sent = serve_file(client_fd,filename,get_mime_type(filename),keep_alive,send_body);

    return res; 
}

const char *format_size(size_t bytes,double *value){
    if(bytes>=1024ULL*1024*1024){
        *value = bytes/(1024.0*1024*1024);
        return "GB";
    }
    if(bytes>=1024*1024){
        *value = bytes/(1024.0*1024.0);
        return "MB";
    }
    if(bytes>=1024){
        *value = bytes/1024.0;
        return "KB";
    }
    *value = (double)bytes;
    return "B";
}

int handle_client(int client_fd){
    // while(1){
        // Receive message
        ssize_t bytes;
        char buffer[BUFFER_SIZE] = {0};

        bytes = recv(client_fd,buffer,BUFFER_SIZE-1,0);
        if(bytes<0){
            if(errno!=EAGAIN && errno!=EWOULDBLOCK){
                perror("recv");
            }
            return 0;
        }else if(bytes==0){
            //TCP uses a FIN packet to say: "I have no more data to send."
            // So bytes=0 , and client closed the connection 
            // printf("[%s:%d] Client closed connection.\n", client_ip, client_port);
            // reason = CLOSE_HTTP_CLOSE;
            return 0;
        }

        buffer[bytes]='\0';


        HttpRequest request;
        if (parse_http_request(buffer, &request) != 0){
            return 0;
        }

        int keep_alive = should_keep_alive(&request);

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        ResponseInfo res = route_request(client_fd,&request,keep_alive);

        clock_gettime(CLOCK_MONOTONIC, &end);

        double elapsed_ms =
            (end.tv_sec-start.tv_sec)*1000.0 +
            (end.tv_nsec-start.tv_nsec)/1000000.0;

        double size_value;
        const char *size_unit = format_size(res.bytes_sent, &size_value);

        printf("%-6s %-20s %-6d %7.1f %-2s %8.3f ms\n",
       request.method,
       request.path,
       res.status_code,
       size_value,
       size_unit,
       elapsed_ms);

       clients[client_fd].last_activity = time(NULL);


    // }   
    return keep_alive;

}

int main(){

    int server_fd = create_server_socket();
    // It's a prerequisite for epoll,   
    // Non-blocking mode makes those calls return immediately with EAGAIN/EWOULDBLOCK if nothing's available,
    // so your loop never stalls on one fd while others are ready.
    set_nonblocking(server_fd);

    // A kernel object that stores all the file descriptors you want Linux to monitor.
    // Creates the epoll instance itself — an in-kernel data structure
    // (historically a red-black tree + ready list) that tracks a set of fds you care about.
    // epoll_fd is your handle to that structure. The 0 argument is just flags (you're not passing EPOLL_CLOEXEC).

    // default - Level-Triggered ( which means , until data is read fully from a buffer, it keeps on sending that fd is readable)
    // while Event-Triggered, tells that only once. 

    // This means with ET, your code has a hard requirement:
    // whenever you get an EPOLLIN event, you must loop recv() (or accept()) until it returns EAGAIN/EWOULDBLOCK, 
    // in the same way your accept-loop already does. 
    // If you stop early — say you recv() once and there's still 500 bytes left in the buffer —
    // epoll will never tell you again, and that connection just silently stalls forever (or until more data happens to arrive and re-trigger the edge).
    
    // ET is generally more efficient at very high fd counts / high event rates,
    // because the kernel does less bookkeeping

    int epoll_fd = epoll_create1(0);
    if(epoll_fd==-1){
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev;

    ev.events = EPOLLIN;
    ev.data.fd = server_fd;

    // The kernel stores this registration in its internal epoll data structure.
    if(epoll_ctl(epoll_fd,EPOLL_CTL_ADD,server_fd,&ev)==-1){
        // adds  server_fd to the set of fds you're watching for me, under this epoll instance (epoll_fd)
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    struct epoll_event events[MAX_EVENTS]; // watches for max_events number of ready events . like a batch

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
    }

    while(1){
        int ready = epoll_wait(epoll_fd,events,MAX_EVENTS,1000); //1000 indicates wake up every second, run cleanup, go back to sleep 
        // epoll_wait() fills events[] with only the fds that are currently in one of these "ready" states, and returns ready, the count. 
        // That's the efficiency win — you don't scan every registered fd, only the ones that actually have something to do.
        if(ready==-1){
            perror("EPOLL_WAIT");
            continue;
        }
        cleanup_idle_clients(epoll_fd);
        for(int i=0;i<ready;i++){
            if(events[i].data.fd==server_fd){   
                // server_fd (the listening socket) is readable →
                // a new incoming connection is waiting in the accept queue.
                while(1){
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    
                    int client_fd = accept(server_fd,(struct sockaddr*)&client_addr,&client_len);
                    
                    if(client_fd==-1){
                        if(errno==EAGAIN || errno==EWOULDBLOCK){
                            break;
                        }
                        perror("accept");
                        break;
                    }

                    clients[client_fd].fd = client_fd;
                    clients[client_fd].last_activity = time(NULL);

                    set_nonblocking(client_fd);

                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET,&client_addr.sin_addr,ip,sizeof(ip));

                    strcpy(clients[client_fd].ip, ip);
                    clients[client_fd].port = ntohs(client_addr.sin_port);

                    printf("Client connected from %s:%d\n",ip,ntohs(client_addr.sin_port));

                    struct epoll_event client_event;

                    client_event.events = EPOLLIN;
                    client_event.data.fd = client_fd;

                    if(epoll_ctl(epoll_fd,EPOLL_CTL_ADD,client_fd,&client_event)==-1){
                        perror("epoll_ctl client");
                        close(client_fd);
                        clients[client_fd].fd = -1;
                    }

                }
            }else{
                // a client_fd is readable → the client has sent bytes that are 
                // sitting in the kernel's receive buffer, waiting for you to recv() them.

                int client_fd = events[i].data.fd; 
                int keep_alive = handle_client(client_fd);

                if(!keep_alive){
                    epoll_ctl(epoll_fd,EPOLL_CTL_DEL,client_fd,NULL);
                    clients[client_fd].fd = -1;
                    close(client_fd);
                }
            }
        }
    }

    close(server_fd);    
    return 0;
}