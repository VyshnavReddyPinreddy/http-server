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

void send_response(int client_fd,const char *status,const char *content_type,const char *body){
    char response[4096];
    int body_length=strlen(body);

    snprintf(response,
            "HTTP/1.1 %s\r\n"
            "Content-type: %s\r\n"
            "Content-length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            status,
            content_type,
            body_length,
            body
    );

    send(client_fd,response,strlen(response),0);
}

void route_request(int client_fd,HttpRequest *request){
    if(strcmp(request->path,"/")==0){
        send_response(client_fd,"200 OK","text/plain","Home Page");
    }else if(strcmp(request->path,"/about")==0){
        send_response(client_fd,"200 OK","text/plain","About Page");
    }else{
        send_response(client_fd,"404 Not Found","text/plain","404 Not Found");
    }
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
