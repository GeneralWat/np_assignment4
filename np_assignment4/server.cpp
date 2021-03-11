#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <vector>
#include <time.h>
#include <errno.h>
#include <algorithm>
#include <iostream>

#define BACKLOG 10
#define MAXDATASIZE 256
#define ROCK 1
#define PAPER 2
#define SCISSOR 3


float medAnsTime;

struct client{
    sockaddr_storage clientInfo;
    char IP[INET6_ADDRSTRLEN];
    int port;
    int fdNr;
    bool inGame = false;
    bool inQueue = false;
    bool timeout = false;
    bool spectate = false;
    bool watchingGame = false;
    int rpsPick = 0;
    time_t start;
    time_t recvTime;
    float timeSum = 0;
    float medAnsTime;
};

struct game{
    struct client *clients[2];
    struct client *spectator[4];
    int nrOfSpectators = 0;
    int playersInGame = 0;
    int readyPlayers = 0;
    int score1 = 0;
    int score2 = 0;
    int stage = 0;
    int nrOfRounds = 0;
    int countDown = 4;
};

int nrOfInTop = 0;
float toplist[10];

void addAnswerTime(client *clients){
time_t end = time(NULL);
  float sec = (end - clients->recvTime); 
  clients->timeSum += sec;
}

bool checkClientsTime(client *client){
  time_t end = time(NULL);
  int sec = (end - client->start); 
  bool result = false;
  if(sec >= 2){//Have they answered the last 2 seconds? 
    result = true;
  }
  return result;
}

void checkToplist(client *client){
    if(nrOfInTop == 0){
        toplist[0] = client->medAnsTime;
        nrOfInTop++;
    }else if(nrOfInTop < 10){
        toplist[nrOfInTop] = client->medAnsTime;
        nrOfInTop++;
        float temp;
        for(int i = 0; i < nrOfInTop; ++i){
            int indexOfMin = i;
            for(int k = i + 1; k < nrOfInTop; ++k){
                if(toplist[k] < toplist[indexOfMin]){
                    indexOfMin = k;
                }
            }
            temp = toplist[i];
            toplist[i] = toplist[indexOfMin];
            toplist[indexOfMin] = temp;
        }
    }else if(client->medAnsTime < toplist[nrOfInTop - 1]){
        toplist[nrOfInTop - 1] = client->medAnsTime;
         float temp;
        for(int i = 0; i < nrOfInTop; ++i){
            int indexOfMin = i;
            for(int k = i + 1; k < nrOfInTop; ++k){
                if(toplist[k] < toplist[indexOfMin]){
                    indexOfMin = k;
                }
            }
            temp = toplist[i];
            toplist[i] = toplist[indexOfMin];
            toplist[indexOfMin] = temp;
        }
    }
}

void sendToSpectators(game *games, char msg[]){
    if(games->nrOfSpectators > 0){
        for(int i = 0; i < games->nrOfSpectators; ++i){
            if(send(games->spectator[i]->fdNr, msg, strlen(msg), 0) == -1){
                perror("send");
            }
        }
    }
}

void sendActiveGames(client *clients,int &nrOfGames, char msg[], char out_buf[], game *games[]){
    strcpy(msg, "Type the number of the game you want to spectate\nAvable games:\n");
    if(nrOfGames == 0){//No games up
    clients->spectate = false;
    clients->watchingGame = false;
        sprintf(out_buf, "%sNo games is active, returning to menu\nPlease select:\n1. Play\n2. Watch\n0. Exit\n", msg);
        send(clients->fdNr, out_buf, strlen(out_buf), 0);
    }else{
        clients->watchingGame = false;
        send(clients->fdNr, msg, strlen(msg), 0);
        clients->spectate = true;
        for(int j = 0; j < nrOfGames; ++j){
            sprintf(out_buf, "Game %d: Score %d - %d\n", j + 1, games[j]->score1, games[j]->score2);
            if(send(clients->fdNr, out_buf, strlen(out_buf), 0) == -1){
                perror("Send");
            }
        }
    }
}

void whoWon(game *games, char msg[]){ //Check round score and send to clients
    games->nrOfRounds++;
    if(games->clients[0]->timeout == true && games->clients[1]->timeout == false){
        addAnswerTime(games->clients[0]);
        games->score2++;
        sprintf(msg,"Round %d\nYou timed out!, Other player won round\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
        send(games->clients[0]->fdNr, msg, strlen(msg), 0);
        sprintf(msg,"Rounds %d\nOther player timed out!, You won round\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
        send(games->clients[1]->fdNr, msg, strlen(msg), 0);
        sprintf(msg, "Round %d\nPlayer 1 timed out!\nPlayer 2 automatic win\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
        sendToSpectators(games, msg);
        games->clients[0]->timeout = false;
        printf("Score now is %d vs %d\n", games->score1, games->score2);
    }else if(games->clients[1]->timeout == true && games->clients[0]->timeout == false){
        addAnswerTime(games->clients[1]);
        games->score1++;
        sprintf(msg,"Round %d\nYou timed out!, Other player won round\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
        send(games->clients[1]->fdNr, msg, strlen(msg), 0);
        sprintf(msg,"Round %d\nOther player timed out!, You won round\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
        send(games->clients[0]->fdNr, msg, strlen(msg), 0);
        sprintf(msg, "Round %d\nPlayer 2 timed out!\nPlayer 1 automatic win\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
        sendToSpectators(games, msg);
        games->clients[1]->timeout = false;
        printf("Score now is %d vs %d\n", games->score1, games->score2);
    }else if(games->clients[0]->timeout == true && games->clients[1]->timeout == true){
        games->clients[0]->timeout = false;
        games->clients[1]->timeout = false;
        addAnswerTime(games->clients[0]);
        addAnswerTime(games->clients[1]);
        sprintf(msg,"Round %d\nBoth timed out! Restarting round\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
        for(int i = 0; i < games->readyPlayers; ++i){
            send(games->clients[i]->fdNr, msg, strlen(msg), 0);
        }
        sendToSpectators(games, msg);
        printf("Score now is %d vs %d\n", games->score1, games->score2);
    }else{
        if(games->clients[0]->rpsPick == games->clients[1]->rpsPick && games->clients[0]->rpsPick != 0 && games->clients[1]->rpsPick != 0){
            printf("The result is Equal!\n");
            sprintf(msg, "Round %d\nBoth picked the same!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            for(int i = 0; i < games->readyPlayers; ++i){
                send(games->clients[i]->fdNr, msg, strlen(msg), 0);
            }
            sendToSpectators(games, msg);
        }else if(games->clients[0]->rpsPick == ROCK && games->clients[1]->rpsPick == SCISSOR){
            games->score1++;
            printf("Score now is %d vs %d\n", games->score1, games->score2);
            sprintf(msg, "Round %d\nYou won the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            send(games->clients[0]->fdNr, msg, strlen(msg), 0);
            sprintf(msg, "Round %d\nYou lost the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            send(games->clients[1]->fdNr, msg, strlen(msg), 0);
            sprintf(msg, "Round %d\nPlayer 1 won the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            sendToSpectators(games, msg);
        }else if(games->clients[1]->rpsPick == ROCK && games->clients[0]->rpsPick == SCISSOR){
            games->score2++;
            printf("Score now is %d vs %d\n", games->score1, games->score2);
            sprintf(msg, "Round %d\nYou won the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            send(games->clients[1]->fdNr, msg, strlen(msg), 0);
            sprintf(msg, "Round %d\nYou lost the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            send(games->clients[0]->fdNr, msg, strlen(msg), 0);
            sprintf(msg, "Round %d\nPlayer 2 won the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            sendToSpectators(games, msg);
        }else if(games->clients[0]->rpsPick == SCISSOR && games->clients[1]->rpsPick == PAPER){
            games->score1++;
            printf("Score now is %d vs %d\n", games->score1, games->score2);
            sprintf(msg, "Round %d\nYou won the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            send(games->clients[0]->fdNr, msg, strlen(msg), 0);
            sprintf(msg, "Round %d\nYou lost the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            send(games->clients[1]->fdNr, msg, strlen(msg), 0);
            sprintf(msg, "Round %d\nPlayer 1 won the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            sendToSpectators(games, msg);
        }else if(games->clients[1]->rpsPick == SCISSOR && games->clients[0]->rpsPick == PAPER){
            games->score2++;
            printf("Score now is %d vs %d\n", games->score1, games->score2);
            sprintf(msg, "Round %d\nYou won the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            send(games->clients[1]->fdNr, msg, strlen(msg), 0);
            sprintf(msg, "Round %d\nPlayer 2 won the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            sendToSpectators(games, msg);
            sprintf(msg, "Round %d\nYou lost the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            send(games->clients[0]->fdNr, msg, strlen(msg), 0);
        }else if(games->clients[0]->rpsPick == PAPER && games->clients[1]->rpsPick == ROCK){
            games->score1++;
            printf("Score now is %d vs %d\n", games->score1, games->score2);
            sprintf(msg, "Round %d\nYou won the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            send(games->clients[0]->fdNr, msg, strlen(msg), 0);
            sprintf(msg, "Round %d\nYou lost the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            send(games->clients[1]->fdNr, msg, strlen(msg), 0);
            sprintf(msg, "Round %d\nPlayer 1 won the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            sendToSpectators(games, msg);
        }else if(games->clients[1]->rpsPick == PAPER && games->clients[0]->rpsPick == ROCK){
            games->score2++;
            printf("Score now is %d vs %d\n", games->score1, games->score2);
            sprintf(msg, "Round %d\nYou won the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            send(games->clients[1]->fdNr, msg, strlen(msg), 0);
            sprintf(msg, "Round %d\nYou lost the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            send(games->clients[0]->fdNr, msg, strlen(msg), 0);
            sprintf(msg, "Round %d\nPlayer 2 won the round!\nScore: %d vs %d\n", games->nrOfRounds,games->score1, games->score2);
            sendToSpectators(games, msg);
        }
    }

    games->stage = 1;
}

void rpsMsg(char msg[]){
    strcpy(msg, "Please select:\n1. Rock\n2. Paper\n3. Scissor\n");
}

void menuMsg(char msg[]){
    strcpy(msg, "Please select:\n1. Play\n2. Watch\n3. Toplist\n0. Exit\n");
}

void showToplist(client *client, char msg[], char out_buf[]){
    strcpy(msg, "Toplist\nFastest medium answer time:\n");
    send(client->fdNr, msg, strlen(msg), 0);
    if(nrOfInTop == 0){
        sprintf(out_buf, "No one has made it on the toplist!\n");
        send(client->fdNr, out_buf, strlen(out_buf), 0);
    }else{
        for(int i = 0; i < nrOfInTop; ++i){
            sprintf(out_buf, "%d. %f sec\n",i + 1, toplist[i]);
            send(client->fdNr, out_buf, strlen(out_buf), 0);
        }
    }
}

void removeGame(game *games[], int &nrOfGames, int indexToRemove){
    if(games[indexToRemove] != nullptr){
        delete games[indexToRemove];
        nrOfGames--;
        if(nrOfGames > 1 && indexToRemove != nrOfGames -1){
            games[indexToRemove] = games[nrOfGames];
        }
    }
}

void gameWon(game *game[], char msg[], int &gameIndex, int &nrOfGames){ //Just to check if someone has a score of 3
    char temp[MAXDATASIZE];
    if(game[gameIndex]->score1 == 3 || game[gameIndex]->score2 == 3){
        for(int i = 0; i < game[gameIndex]->readyPlayers; ++i){
            game[gameIndex]->clients[i]->medAnsTime = (float)game[gameIndex]->clients[i]->timeSum / game[gameIndex]->nrOfRounds;
            sprintf(msg, "Your medium answer time was %f sec \n", game[gameIndex]->clients[i]->medAnsTime);
            send(game[gameIndex]->clients[i]->fdNr, msg, strlen(msg),0);
            checkToplist(game[gameIndex]->clients[i]);
            game[gameIndex]->clients[i]->medAnsTime = 0;
            game[gameIndex]->clients[i]->timeSum = 0;
        }
        if(game[gameIndex]->score1 == 3){
            strcpy(msg, "You won!\n");
            if(send(game[gameIndex]->clients[0]->fdNr, msg, strlen(msg), 0) == -1){
                perror("send");
            }
            strcpy(msg, "Player 1 won!\n");
            sendToSpectators(game[gameIndex], msg);
            strcpy(msg, "You lost!\n");//Then player 2 lost
            if(send(game[gameIndex]->clients[1]->fdNr, msg, strlen(msg), 0) == -1){
                perror("send");
            }
        }else if(game[gameIndex]->score2 == 3){
            strcpy(msg, "You won!\n");
            if(send(game[gameIndex]->clients[1]->fdNr, msg, strlen(msg), 0) == -1){
                perror("send");
            }
            strcpy(msg, "Player 2 won!\n");
            sendToSpectators(game[gameIndex], msg);
            strcpy(msg, "You lost!\n");//Then player 2 lost
            if(send(game[gameIndex]->clients[0]->fdNr, msg, strlen(msg), 0) == -1){
                perror("send");
            }
        }
        menuMsg(msg); //Send menu to everyone!
        for(int j = 0; j < game[gameIndex]->readyPlayers; ++j){
            game[gameIndex]->clients[j]->rpsPick = 0;
            game[gameIndex]->clients[j]->inGame = false;
            if(send(game[gameIndex]->clients[j]->fdNr, msg, strlen(msg), 0) == -1){
                perror("send");
            }
        }
        if(game[gameIndex]->nrOfSpectators > 0){
            for(int j = 0; j < game[gameIndex]->nrOfSpectators; ++j){
                sendActiveGames(game[gameIndex]->spectator[j], nrOfGames, msg, temp, game);
                
            }   
        }
        removeGame(game, nrOfGames, gameIndex);
    }
}

void checkJobbList(int signum){
  // As anybody can call the handler, its good coding to check the signal number that called it.
  if(signum == SIGALRM){
    time_t end = time(NULL);
  }
  
  return;
}


void checkClients(game *games[], client *clients[], int &nrOfClients, int &nrOfGames, char msg[]) //Function to check if clients have answered last 2 secs
{
    //printf("Nr of games_ %d\n", nrOfGames);
    if(nrOfGames > 0){
        for(int i = 0; i < nrOfGames; ++i){
            if(games[i]->stage == 1){
                games[i]->countDown--;
                for(int j = 0; j < games[i]->readyPlayers; ++j){
                    if(games[i]->countDown > 0){
                        sprintf(msg, "Game will start in %d seconds\n", games[i]->countDown);
                        send(games[i]->clients[j]->fdNr, msg, strlen(msg), 0);
                        if(j == 0){
                            sendToSpectators(games[i], msg);
                        }
                    }
                    if(games[i]->countDown == 0){ // To wait the extra second
                        games[i]->stage++;
                        rpsMsg(msg);
                        for(int z = 0; z < games[i]->readyPlayers; ++z){
                            send(games[i]->clients[z]->fdNr, msg, strlen(msg), 0);
                            games[i]->clients[z]->recvTime = time(NULL);
                            games[i]->clients[z]->rpsPick = 0;
                            games[i]->clients[z]->start = time(NULL); //Start timer!
                        }
                        sendToSpectators(games[i], msg);
                        games[i]->countDown = 4;
                        break;
                    }
                }
            }
            if(games[i]->stage == 2){
                for(int j = 0; j < games[i]->readyPlayers; ++j){
                    if(games[i]->clients[j]->rpsPick == 0 && checkClientsTime(games[i]->clients[j]) == true){
                        printf("Player %d timeout\n", j + 1);
                        games[i]->clients[j]->timeout = true;
                    }
                }
            }
        }
        for(int i = 0; i < nrOfGames; ++i){
            if(games[i]->stage == 2){
                for(int j = 0; j < games[i]->readyPlayers; ++j){
                    if(games[i]->clients[j]->timeout ){
                        whoWon(games[i], msg);
                        gameWon(games, msg, i, nrOfGames);
                        games[i]->stage = 1;
                        break;
                    }
                }
            }
        }
    }

}

void expandArr(client *clients[], int &nrOfClients, int &capacity){ //need one for the games aswell
    capacity += 10;
    client *temp[capacity];
    for(int i = 0; i < nrOfClients; ++i){
        temp[i] = clients[i];
    }
    delete[] clients;
    clients = temp;

}

void freeClient(client *clients[], int &nrOfClients, int indexToRemove){
    if(clients[indexToRemove] != nullptr){
        delete clients[indexToRemove];
        if(nrOfClients > 1 && indexToRemove != nrOfClients - 1){
            clients[indexToRemove] = clients[nrOfClients - 1]; 
        }
        nrOfClients--;
        printf("clients left: %d \n", nrOfClients);
    }
}


void resetClient(game *games[], client *clients[], int nrOf, int clientIndex, int gameIndex){
    for(int i = 0; i < nrOf; ++i){
        if(games[gameIndex]->clients[clientIndex]->fdNr == clients[i]->fdNr){
            clients[i]->inGame = false;
            printf("In game: %d\n", clients[i]->inGame);
            clients[i]->inQueue = false;
            clients[i]->rpsPick = 0;
        }
    }
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[]){
    if(argc != 2){
    fprintf(stderr, "Usage: <IP>:<port>\n");
    exit(1);
    }

    char *hoststring,*portstring, *rest, *org;
    org=strdup(argv[1]);
    rest=argv[1];
    hoststring=strtok_r(rest,":",&rest);
    portstring=strtok_r(rest,":",&rest);
    printf("Got %s split into %s and %s \n",org, hoststring,portstring);

    if(hoststring == NULL || portstring == NULL){
        fprintf(stderr, "You must enter a IP adress and Port");
	    exit(1);
    }

    fd_set master;
    fd_set read_fds;

    int fd_max;

    int listener, new_fd;
    struct sockaddr_storage remoteAddr;
    socklen_t addrlen;
    struct sockaddr_in *the_addr;

    char out_buf[MAXDATASIZE + 20];
    char in_buf[MAXDATASIZE];
    char msg[MAXDATASIZE];
    char command[20];
    int numbytes;
    int gameIndex;
    int queue = 0;
    int index;

    int gameCap = 10;
    int capacity = 10;
    struct client *clients[capacity] = {nullptr};
    struct game *games[gameCap] = {nullptr};
    int nrOfClients = 0;
    int nrOfGames = 0;

    char remoteIP[INET6_ADDRSTRLEN];
    int yes = 1;
    int rv;
    struct addrinfo hints, *servinfo, *p;

    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if((rv = getaddrinfo(hoststring, portstring, &hints, &servinfo)) != 0){
        fprintf(stderr, "Server: %s\n", gai_strerror(rv));
        exit(1);
    }

    for(p = servinfo; p != NULL; p = p->ai_next){

        if((listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            perror("Server: socket");
            continue;
        }
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if(bind(listener, p->ai_addr, p->ai_addrlen) < 0){
            close(listener);
            exit(1);
        }
        break;
    }
    if(p == NULL){
        fprintf(stderr, "Server: Failed to bind\n");
        exit(1);
    }
    freeaddrinfo(servinfo);
    if(listen(listener, BACKLOG) == -1){
        perror("Listen");
        exit(1);
    }

    //void checkJobbList(int signum);
    struct itimerval alarmTime;
    alarmTime.it_interval.tv_sec=1;
    alarmTime.it_interval.tv_usec=1;
    alarmTime.it_value.tv_sec=1;
    alarmTime.it_value.tv_usec=1;

    /* Regiter a callback function, associated with the SIGALRM signal, which will be raised when the alarm goes of */
    struct sigaction sa; 
    sa.sa_handler = checkJobbList;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    sigset_t sigset,oldset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &sigset, &oldset);
    setitimer(ITIMER_REAL,&alarmTime,NULL);

    

    FD_SET(listener, &master);
    fd_max = listener;
    while(1){
        read_fds = master;
        if(pselect(fd_max + 1,&read_fds, NULL, NULL, NULL, &oldset) == -1){
            if(errno == EINTR){ //If SIGALRM is set off
                //Check on clients and stuff
                checkClients(games, clients, nrOfClients, nrOfGames, msg); //Check all clients every sec
                continue;
            }else{
            perror("Select");
            exit(1);
            }
        }
        for(int i = 0; i <= fd_max; ++i){
            if(FD_ISSET(i, &read_fds)){
                if(i == listener){ // accept new connections
                    addrlen = sizeof remoteAddr;
                    new_fd = accept(listener, (struct sockaddr*)&remoteAddr, &addrlen);
                    if(new_fd == -1){
                        perror("accept");
                    }else{
                        FD_SET(new_fd, &master);
                        if(new_fd > fd_max){
                            fd_max = new_fd;
                        }
                        the_addr = (struct sockaddr_in*)&remoteAddr;
                        printf("New connection from: %s:%d\n", inet_ntop(remoteAddr.ss_family, 
                        get_in_addr((struct sockaddr*)&remoteAddr), remoteIP, INET6_ADDRSTRLEN), the_addr->sin_port);
                        strcpy(out_buf, "RPS TCP 1\n");
                        if((numbytes = send(new_fd, &out_buf, strlen(out_buf), 0)) == -1){
                            perror("Send");
                        }
                        out_buf[numbytes - 1] = '\0';
                        if(nrOfClients  == capacity){
                            expandArr(clients, nrOfClients, capacity);
                        }
                        clients[nrOfClients] = new client;
                        clients[nrOfClients]->clientInfo = remoteAddr;
                        clients[nrOfClients]->port = ntohs(the_addr->sin_port);
                        inet_ntop(remoteAddr.ss_family, get_in_addr((struct sockaddr*)&remoteAddr), clients[nrOfClients]->IP, INET6_ADDRSTRLEN);
                        clients[nrOfClients]->fdNr = new_fd;
                        nrOfClients++;
                    }
                }else { // handle data
                    if((numbytes = recv(i, &in_buf, MAXDATASIZE - 1, 0))<= 0){
                        if(numbytes == 0){
                            printf("Client left\n");
                        }else{
                            perror("recv");
                        }
                        for(int j = 0; j <= nrOfClients; ++j){ // Remove clients and fix clients in game if thats the case
                            if(clients[j]->fdNr == i){
                                if(clients[j]->inGame == true){
                                    clients[j]->inGame = false;
                                    for(int x = 0; x < nrOfGames; ++x){ //FInd the game that the client is in
                                        if(games[x]->clients[0]->fdNr == i){
                                            //Delete game
                                            resetClient(games, clients, nrOfClients, 1, x);
                                            menuMsg(out_buf);
                                            if(send(games[x]->clients[1]->fdNr, out_buf, strlen(out_buf), 0) == -1){
                                                perror("send");
                                            }
                                            removeGame(games, nrOfGames, x);
                                        }else if(games[x]->clients[1]->fdNr == i){
                                            resetClient(games, clients, nrOfClients, 0, x);
                                            menuMsg(out_buf);
                                            if(send(games[x]->clients[0]->fdNr, out_buf, strlen(out_buf), 0) == -1){
                                                perror("send");
                                            }
                                            removeGame(games, nrOfGames, x);
                                        }
                                        if(games[x]->nrOfSpectators > 0){
                                            for(int z = 0; z < games[x]->nrOfSpectators; ++z){
                                                sendActiveGames(clients[index], nrOfGames, msg, out_buf, games);
                                            }
                                        }
                                    }
                                }
                                if(clients[j]->inQueue == true){
                                    queue--;
                                }
                                if(clients[j]->spectate == true){
                                    for(int z = 0; z < nrOfGames; ++z){
                                        for(int j = 0; j < games[z]->nrOfSpectators; ++j){
                                            if(games[z]->spectator[j]->fdNr == i){
                                                games[z]->nrOfSpectators--;
                                                if(j != games[z]->nrOfSpectators){
                                                    games[z]->spectator[j] = games[z]->spectator[games[z]->nrOfSpectators];
                                                }
                                                break;
                                            }
                                        }
                                    }
                                }
                                freeClient(clients, nrOfClients, j);//Set the client free!
                            }
                        }
                        close(i);
                        FD_CLR(i, &master);
                    }else{
                        in_buf[numbytes - 1] = '\0';
                        for(int j = 0; j < nrOfClients; ++j){ //Find index of client who sent message and use 
                            if(clients[j]->fdNr == i){
                                index = j;     
                            }
                        }
                        sscanf(in_buf, "%s %s", command, msg);
                        if(strcmp(command, "OPT") == 0 && clients[index]->inGame == false && clients[index]->inQueue == false && clients[index]->spectate == false){
                            if(strcmp(msg, "OK") == 0){
                                menuMsg(out_buf);
                                printf("Sending menu\n");
                                if((numbytes = send(i, out_buf, strlen(out_buf), 0)) == -1){
                                    perror("Send");
                                }
                            }else if(strcmp(msg, "0") == 0){//If client entered 0
                                freeClient(clients, nrOfClients, index);
                                close(i);
                                FD_CLR(i, &master);
                            }else if(strcmp(msg, "1") == 0){ // add to queue
                                    queue+= 1;
                                    clients[index]->inQueue = true;
                                    printf("in queue: %d\n", queue);
                                if(queue >= 2){ //if queue is greater or equal to 2, start a games
                                    games[nrOfGames] = new game{nullptr};
                                    for(int j = 0; j < nrOfClients && games[nrOfGames]->playersInGame != 2; ++j){
                                        if(clients[j]->inQueue == true){
                                            clients[j]->inGame = true;
                                            clients[j]->inQueue = false;
                                            games[nrOfGames]->playersInGame++;
                                            if(games[nrOfGames]->clients[0] == nullptr){
                                                games[nrOfGames]->clients[0] = clients[j];
                                            }else{
                                                games[nrOfGames]->clients[1] = clients[j];
                                            }
                                        }
                                    }
                                    queue -=2;
                                    for(int j = 0; j < games[nrOfGames]->playersInGame; ++j){
                                        strcpy(out_buf, "Write 'ready' if you are ready:\n");
                                        if(send(games[nrOfGames]->clients[j]->fdNr, out_buf, strlen(out_buf), 0) == -1){
                                            perror("send");
                                        }
                                    }
                                    nrOfGames++;
                                    
                                }
                            }else if(strcmp(msg, "2") == 0){
                                clients[index]->spectate = true;
                                sendActiveGames(clients[index], nrOfGames, msg, out_buf, games);
                            }else if(strcmp(msg, "3") == 0){//show toplist!
                                showToplist(clients[index], msg, out_buf);
                                menuMsg(msg);
                                send(clients[index]->fdNr, msg, strlen(msg), 0);
                            }
                        }
                        if(strcmp(command, "OPT") == 0 && clients[index]->spectate == true && clients[index]->watchingGame == false){
                            if(nrOfGames == 0){
                                sendActiveGames(clients[index], nrOfGames, msg, out_buf, games);
                            }else if(atoi(msg) <= nrOfGames && atoi(msg) > 0){
                                for(int j = 0; j < nrOfGames; ++j){
                                    if(atoi(msg) == j + 1){
                                        games[j]->spectator[games[j]->nrOfSpectators] = clients[index];
                                        games[j]->nrOfSpectators++;
                                        clients[index]->watchingGame = true;
                                        break;
                                    }
                                }  
                            } 
                            
                        }else if(clients[index]->spectate == true && strlen(msg) > 0 && clients[index]->watchingGame == true){
                            for(int z = 0; z < nrOfGames; ++z){
                                for(int j = 0; j < games[z]->nrOfSpectators; ++j){
                                    if(games[z]->spectator[j]->fdNr == i){
                                        games[z]->nrOfSpectators--;
                                        if(j != games[z]->nrOfSpectators){
                                            games[z]->spectator[j] = games[z]->spectator[games[z]->nrOfSpectators];
                                        }
                                        break;
                                    }
                                }
                            }
                            sendActiveGames(clients[index], nrOfGames, msg, out_buf, games);
                            clients[index]->watchingGame = false;
                        }
                        if(nrOfGames > 0){
                            for(int j = 0; j < nrOfGames; ++j){
                                if(clients[index]->fdNr == games[j]->clients[0]->fdNr || clients[index]->fdNr == games[j]->clients[1]->fdNr){
                                    gameIndex = j;
                                }
                        }
                        if(strcmp(msg, "ready") == 0 && clients[index]->inGame == true && games[gameIndex]->stage == 0){ // if they are ready, send rock, paper. scissor msg
                            
                            if(games[gameIndex]->clients[0]->fdNr == i){
                               games[gameIndex]->readyPlayers++;
                            }else if(games[gameIndex]->clients[1]->fdNr == i){
                                games[gameIndex]->readyPlayers++;
                            }
                            if(games[gameIndex]->readyPlayers == 2){
                                games[gameIndex]->stage = 1;
                            }
                        }
                        if(games[gameIndex]->stage == 2 && clients[index]->inGame == true && clients[index]->rpsPick == 0){ // Client gets to choose rock paper or scissor
                            if(strcmp(msg, "1") == 0 && clients[index]->inGame == true){
                                printf("picked Rock\n");
                                clients[index]->rpsPick = 1;
                                addAnswerTime(clients[index]);
                            }else if(strcmp(msg, "2") == 0 && clients[index]->inGame == true){
                                printf("picked Paper\n");
                                clients[index]->rpsPick = 2;
                                addAnswerTime(clients[index]);
                            }else if(strcmp(msg, "3") == 0 && clients[index]->inGame == true){
                                printf("picked Scissor\n");
                                clients[index]->rpsPick = 3;
                                addAnswerTime(clients[index]);
                            }
                        }
                        }
                        if(nrOfGames > 0){
                            for(int j = 0; j < nrOfGames; ++j){ //set gameIndex to clients game
                                if(clients[index]->fdNr == games[j]->clients[0]->fdNr || clients[index]->fdNr == games[j]->clients[1]->fdNr){
                                    gameIndex = j;
                                }
                            }
                            if(games[gameIndex]->clients[0]->rpsPick != 0 && games[gameIndex]->clients[1]->rpsPick != 0){// both picked! Check who won
                                games[gameIndex]->stage++;
                                printf("The picks were: %d vs %d\n", games[gameIndex]->clients[0]->rpsPick, games[gameIndex]->clients[1]->rpsPick);
                                sprintf(out_buf, "The picks were: %d vs %d\n", games[gameIndex]->clients[0]->rpsPick, games[gameIndex]->clients[1]->rpsPick);
                                for(int j = 0; j < games[gameIndex]->readyPlayers; ++j){
                                    if(send(games[gameIndex]->clients[j]->fdNr, out_buf, strlen(out_buf), 0) == -1){
                                        perror("send");
                                    }
                                }
                                whoWon(games[gameIndex], out_buf); // Check who won round and increase score 
                                gameWon(games, out_buf, gameIndex, nrOfGames); //did someone reach score 3? Also sending correct msg to everyone
                                if(games[gameIndex]->score1 == 3 || games[gameIndex]->score2 == 3){
                                    if(games[gameIndex]->nrOfSpectators > 0){
                                        for(int j = 0;j < games[gameIndex]->nrOfSpectators; ++j){
                                            sendActiveGames(games[gameIndex]->spectator[j], nrOfGames, msg, out_buf, games);
                                        }
                                    }
                                    removeGame(games, nrOfGames, gameIndex);
                                }
                            }
                        
                        }
                    }
                }
            }
        }
    }


    close(listener);
    free(org);
    return 0;
}
