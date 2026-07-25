#include<stdio.h>
#include<unistd.h>
#include<sys/socket.h>
#include<stdlib.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<string.h>
#include<pthread.h>
#include<sys/time.h>
#include<errno.h>
#include <limits.h> // Gives PATH_MAX

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_HEADERS 32
#define DOCUMENT_ROOT "www"

enum CloseReason {
    CLOSE_TIMEOUT,
    CLOSE_CLIENT,
    CLOSE_ERROR,
    CLOSE_HTTP_ERROR,
    CLOSE_HTTP_CLOSE
};

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
    int client_fd;
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
} ClientInfo;

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

int accept_client(int server_fd, struct sockaddr_in *client_addr){
    socklen_t client_len = sizeof(*client_addr);

    // Accept one client
    int client_fd = accept(server_fd,(struct sockaddr*)client_addr,&client_len);

    if(client_fd<0){
        perror("Accept");
        close(server_fd);
        exit(EXIT_FAILURE);
    }    

    char ip[INET_ADDRSTRLEN];

    inet_ntop(AF_INET,&client_addr->sin_addr,ip,sizeof(ip)); // to get ip address of client

    printf("Client connected from %s:%d\n",ip,ntohs(client_addr->sin_port));
    return client_fd;
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
    void *body = NULL;
    size_t body_length = 0;

    if(read_file(filename,&body,&body_length)!=0){
        // const char *msg = "404 Not Found";
        // send_response(client_fd,"404 Not Found","text/plain",msg,strlen(msg),keep_alive,send_body);
        // return strlen(msg);
        perror("read_file");
        return 0;
    }
    // printf("Read %d bytes from %s\n", bytes, filename);
    send_response(client_fd,"200 OK",content_type,body,body_length,keep_alive,send_body);

    free(body);
    return body_length;
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

void *handle_client(void *arg){
    ClientInfo *client = (ClientInfo*)arg;

    int client_fd = client->client_fd;
    char client_ip[INET_ADDRSTRLEN];
    strcpy(client_ip,client->client_ip);
    int client_port = client->client_port;

    free(client);  

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    setsockopt(client_fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));

    enum CloseReason reason = CLOSE_CLIENT;

    while(1){
        // Receive message
        ssize_t bytes;
        char buffer[BUFFER_SIZE] = {0};

        bytes = recv(client_fd,buffer,BUFFER_SIZE-1,0);
        if(bytes<0){
            if(errno==EAGAIN || errno==EWOULDBLOCK){
                reason = CLOSE_TIMEOUT;
            }else{
                perror("recv");
                reason = CLOSE_ERROR;
            }
            break;
        }else if(bytes==0){
            //TCP uses a FIN packet to say: "I have no more data to send."
            // So bytes=0 , and client closed the connection 
            // printf("[%s:%d] Client closed connection.\n", client_ip, client_port);
            reason = CLOSE_HTTP_CLOSE;
            break;
        }

        buffer[bytes]='\0';

        // printf("\n========== HTTP REQUEST ==========\n");
        // printf("%s\r\n",buffer);
        // printf("==================================\n");

        HttpRequest request;
        if (parse_http_request(buffer, &request) != 0){
            reason = CLOSE_HTTP_ERROR;
            break;
        }

        int keep_alive = should_keep_alive(&request);

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        ResponseInfo res = route_request(client_fd,&request,keep_alive);

        clock_gettime(CLOCK_MONOTONIC, &end);

        double elapsed_ms = (end.tv_sec-start.tv_sec)*1000.0 + (end.tv_nsec-start.tv_nsec)/1000000.0;

        char client_addr[64];
        snprintf(client_addr,
                sizeof(client_addr),
                "%s:%d",
                client_ip,
                client_port);   

        double size_value;
        const char *size_unit = format_size(res.bytes_sent, &size_value);

        printf("%-21s %-6s %-20s %-6d %7.1f %-2s %8.3f ms\n",
                                                            client_addr,
                                                            request.method,
                                                            request.path,
                                                            res.status_code,
                                                            size_value,
                                                            size_unit,
                                                            elapsed_ms);

        if(!keep_alive){
            reason = CLOSE_CLIENT;
            break;
        }
    }   

    switch (reason) {
        case CLOSE_TIMEOUT:
            printf("[%s:%d] Connection closed (Keep-Alive timeout).\n",
                client_ip, client_port);
            break;

        case CLOSE_CLIENT:
            printf("[%s:%d] Connection closed by client.\n",
                client_ip, client_port);
            break;

        case CLOSE_ERROR:
            printf("[%s:%d] Connection closed due to recv error.\n",
                client_ip, client_port);
            break;

        case CLOSE_HTTP_ERROR:
            printf("[%s:%d] Connection closed due to HTTP request error.\n",
                client_ip, client_port);
            break;
        case CLOSE_HTTP_CLOSE:
            printf("[%s:%d] Connection closed (HTTP Connection: close).\n",
                client_ip, client_port);
            break;
    }   
    close(client_fd);   
    return NULL;
}

int main(){
    int server_fd = create_server_socket();
    while(1){
        struct sockaddr_in client_addr;
        //below line waits until TCP three way handshake is done
        int client_fd = accept_client(server_fd,&client_addr);
        
        pthread_t tid;

        ClientInfo *client = malloc(sizeof(ClientInfo));
        if (client == NULL) {
            perror("malloc");
            close(client_fd);
            continue;
        }
        client->client_fd = client_fd;

        inet_ntop(AF_INET,
                &client_addr.sin_addr,
                client->client_ip,
                sizeof(client->client_ip));

        client->client_port = ntohs(client_addr.sin_port);

        if (pthread_create(&tid, NULL, handle_client, client) != 0) {
            perror("pthread_create");
            free(client);
            close(client_fd);
            continue;
        }

        pthread_detach(tid);    
    }

    close(server_fd);    
    return 0;
}
