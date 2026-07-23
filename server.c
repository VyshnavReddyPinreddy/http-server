#include<stdio.h>
#include<unistd.h>
#include<sys/socket.h>
#include<stdlib.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<string.h>
#include<pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_HEADERS 32

typedef struct
{
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

void send_response(int client_fd,const char *status,const char *content_type,const void *body,size_t body_length){
    char header[1024];
    int header_length = snprintf(header,
                                sizeof(header),
                                "HTTP/1.1 %s\r\n"
                                "Content-type: %s\r\n"
                                "Content-Length: %zu\r\n"
                                "Connection: close\r\n"
                                "\r\n",
                                status,
                                content_type,
                                body_length
                                );
    
    if(send(client_fd,header,header_length,0)<0){
        perror("send");
        return;
    }                           
    if(body_length>0){  
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

void serve_file(int client_fd,const char*filename,const char *content_type){
    void *body = NULL;
    size_t body_length = 0;

    if(read_file(filename,&body,&body_length)!=0){
        const char *msg = "404 Not Found";
        send_response(client_fd,"404 Not Found","text/plain",msg,strlen(msg));
        return;
    }
    // printf("Read %d bytes from %s\n", bytes, filename);
    send_response(client_fd,"200 OK",content_type,body,body_length);

    free(body);
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

void route_request(int client_fd,HttpRequest *request){
    char filename[512];

    if(strcmp(request->path,"/")==0){
        strcpy(filename,"www/index.html");
    }else{
        snprintf(filename,sizeof(filename),"www%s",request->path);
    }

    serve_file(client_fd,filename,get_mime_type(filename));
}

void *handle_client(void *arg){
    int client_fd = *(int*)arg;
    free(arg);

    // Receive message
    ssize_t bytes;
    char buffer[BUFFER_SIZE] = {0};

    // For client.c file
    
    // while((bytes = recv(client_fd,buffer,BUFFER_SIZE-1,0)) > 0){
    //     buffer[bytes] = '\0';
    //     printf("Received : %s\n",buffer);
        
    //     if(send(client_fd,buffer,bytes,0)==-1) {
    //         perror("send");
    //         break;
    //     }
        
    //     printf("Reply sent.\n");
    // }

    // For browswer clients 

    bytes = recv(client_fd,buffer,BUFFER_SIZE-1,0);
    if(bytes<=0){
        close(client_fd);
        return NULL;
    }

    buffer[bytes]='\0';

    // printf("\n==========Actual Request==========\n");
    // printf("%s\n", buffer);
    // printf("==================================\n");

    printf("\n========== HTTP REQUEST ==========\n");
    
    HttpRequest request;
    if (parse_http_request(buffer, &request) != 0){
        printf("Invalid HTTP Request\n");
        close(client_fd);
        return NULL;
    }
    printf("Method  : %s\n", request.method);
    printf("Path    : %s\n", request.path);
    printf("Version : %s\n", request.version);
    printf("\nHeaders\n");

    for(int i=0;i<request.header_count;i++){
        printf("%s => %s\n",
            request.headers[i].key,
            request.headers[i].value);
    }
    printf("==================================\n");

    // const char *response =
    // "HTTP/1.1 200 OK\r\n"
    // "Content-Type: text/plain\r\n"
    // "Content-Length: 13\r\n"
    // "Connection: close\r\n"
    // "\r\n"
    // "Hello, World!";

    // if(send(client_fd,response,strlen(response),0)<0){
    //     perror("send");
    // }

    route_request(client_fd,&request);

    printf("Client disconnected.\n");
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

        int *client_socket = malloc(sizeof(int));
        if (client_socket == NULL) {
            perror("malloc");
            close(client_fd);
            continue;
        }
        *client_socket = client_fd;

        if (pthread_create(&tid, NULL, handle_client, client_socket) != 0) {
            perror("pthread_create");
            free(client_socket);
            close(client_fd);
            continue;
        }

        pthread_detach(tid);    
    }

    close(server_fd);    
    return 0;
}
