#include<arpa/inet.h>
#include<netinet/in.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<unistd.h>

#define PORT 8080
#define BUFFER_SIZE 1024
    
int main(){
    
    int sockfd;
    struct sockaddr_in server_addr;
    
    char buffer[BUFFER_SIZE] = {0};
    sockfd = socket(AF_INET,SOCK_STREAM,0);
    
    if(sockfd<0){
        perror("Socket");
        exit(EXIT_FAILURE);
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    
    if(inet_pton(AF_INET,"127.0.0.1",&server_addr.sin_addr)<=0){
        perror("inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    if(connect(sockfd,(struct sockaddr*)&server_addr,sizeof(server_addr))<0){
        perror("Connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server\n");

    // const char *message = "Hello Server!";
    char reply[1024] = {0};

    while(1){
        printf("Enter Message : ");
        fgets(reply,sizeof(reply),stdin);

        // to remove the newline character from reply
        reply[strcspn(reply, "\n")] = '\0';

        if(strlen(reply)==0) continue;

        if(strcmp(reply,"exit")==0){
            close(sockfd);
            return 0;
        }
        
        send(sockfd,reply,strlen(reply),0);
    
        ssize_t bytes = recv(sockfd,buffer,BUFFER_SIZE-1,0);
        
        if(bytes>0){
            buffer[bytes]='\0';
            printf("Server replied : %s\n",buffer);
        }
    }

    close(sockfd);
    return 0;
}
