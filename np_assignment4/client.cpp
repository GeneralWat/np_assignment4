#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define MAXDATASIZE 256

/*Just a simple client. 
    Able to write and to send
    using Format 'OPT' 'OPTION' 
*/

int main(int argc, char *argv[]){

    if(argc != 2){
        printf("USage: <ip>:<port>\n");
        exit(1);
    }

    char delim[]=":";
    char *Desthost=strtok(argv[1],delim);
    char *Destport=strtok(NULL,delim);

    if(Desthost == NULL || Destport == NULL){
	  fprintf(stderr, "You must enter a IP adress and a Port");
	  exit(1);
    }

    int sockfd, fd_max;
    struct addrinfo hints, *servinfo, *p;
    fd_set master;
    fd_set read_fds;
    int rv, numbytes;
    char in_buf[MAXDATASIZE];
    char read_buf[MAXDATASIZE];
    char out_buf[MAXDATASIZE];


    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if((rv = getaddrinfo(Desthost, Destport, &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo %s \n", gai_strerror(rv));
        exit(1);
    }
    for(p = servinfo; p != NULL; p = p->ai_next){
        if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            perror("Talker");
            continue;
        }
        break;
    }
    if(p == NULL){
        fprintf(stderr, "Failed to create socket\n");
        exit(1);
    }
    freeaddrinfo(servinfo);
    fcntl(sockfd, F_SETFD, O_NONBLOCK);

    if(connect(sockfd, p->ai_addr, p->ai_addrlen) < 0){
        perror("talker: connect \n");
        exit(1);
    }
    if((numbytes = recv(sockfd, &in_buf, sizeof(in_buf), 0)) == -1){
        fprintf(stderr, "recv error\n");
        close(sockfd);
        exit(1);
    }
    in_buf[numbytes - 1] = '\0';
    printf("Server protocol: %s\n", in_buf);
    if(strcmp(in_buf, "RPS TCP 1") == 0){
        printf("Protocol supported\n");
        strcpy(out_buf, "OPT OK\n");
        if(send(sockfd, &out_buf, strlen(out_buf), 0) == -1){
            fprintf(stderr, "Send error\n");
            close(sockfd);
            exit(1);
        }
    }else{
        printf("Not supported\n");
        close(sockfd);
        exit(1);
    }
    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    FD_SET(0, &master);
    FD_SET(sockfd, &master);
    fd_max = sockfd;

    while(1){
        read_fds = master;
        if(select(fd_max + 1, &read_fds, NULL, NULL, NULL) == -1){
            perror("Select");
            exit(1);
        }
        for(int i = 0; i <= fd_max; ++i){
            if(FD_ISSET(i, &read_fds)){
                if(i == 0){
                    fgets(read_buf, MAXDATASIZE, stdin);
                    sprintf(out_buf, "OPT %s\n", read_buf);
                    if((numbytes = send(sockfd, &out_buf, strlen(out_buf), 0)) == -1){
                        fprintf(stderr, "Send Error\n");
                        close(sockfd);
                        exit(1);
                    }
                    out_buf[numbytes - 1] = '\0';
                }else{
                    if((numbytes = recv(sockfd, &in_buf, sizeof(in_buf), 0)) == -1){
                        fprintf(stderr, "recv Error\n");
                        close(sockfd);
                        exit(1);
                    }
                    in_buf[numbytes -1] = '\0';
                    if(numbytes == 0){
                        close(sockfd);
                        exit(1);
                    }
                    printf("%s \n", in_buf);
                    fflush(stdout);
                }
            }
        }
    }

    return 0;
}