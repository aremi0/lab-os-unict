/**
 * @file grep.c
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2022-07-15
 * 
 * @copyright Copyright (c) 2022
 * 
 * Prova di laboratorio di SO del 2022-01-28
 *  P.s. stampa l'ultima parola per 2 volte :/
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <wait.h>

#define DIMBUF 256

typedef struct{
    long type;
    int eof;
    char text[DIMBUF];
} msg;

void reader(int pipe[2], char *path){
    int file, size;
    struct stat statbuff;
    char buffer[DIMBUF];
    char *p; //per la mmap
    
    if((file = open(path, O_RDONLY)) == -1){ //apertura file input
        perror("open reader");
        exit(1);
    }

    if(fstat(file, &statbuff) == -1){ //ottengo info sul file (mi serve la sua dimensione per mapparlo in memoria)
        perror("lstat reader");
        exit(1);
    }

    size = statbuff.st_size; //ottengo dimensione file...
    //...mappo il file in memoria centrale...
    if((p = (char*)mmap(NULL, size, PROT_READ, MAP_SHARED, file, 0)) == NULL){
        perror("mmap reader");
        exit(1);
    }

    close(file); //adesso che ho il file in memoria posso chiuderlo

    for(int i = 0; i < size; i+=strlen(buffer)){ //leggo DIMBUF caratteri, write(pipe) ==>NON RIMUOVERE '\n' PERCHE' LA PIPE E' UN VETTORE!!
        strncpy(buffer, p+i, DIMBUF-1);
        write(pipe[1], buffer, strlen(buffer)); //scrivo sulla pipe...
    }

    //in chiusura...
    munmap(p, size);
    close(pipe[0]);
    close(pipe[1]);
    printf("X [READER] in chiusura...\n");
    exit(0);
}

void writer(int coda){
    msg messaggio;
    messaggio.eof = 0;

    while(!messaggio.eof){
        if(msgrcv(coda, &messaggio, sizeof(msg)-sizeof(long), 0, 0) == -1){
            perror("msgrcv writer");
            exit(1);
        }

        printf("[WRITER] messaggio ricevuto: %s", messaggio.text);
    }

    //in chiusura...
    printf("X [WRITER] in chiusura\n");
    exit(0);
}

int main(int argc, char *argv[]){
    msg messaggio;
    FILE *_pipe;
    int pipe_d[2], coda_d, n = 0;
    char buffer[DIMBUF];
    char *token;

    if(argc != 3){
        printf("Uso: %s <parola> <input.txt>\n", argv[0]);
        exit(1);
    }

    if((coda_d = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione coda
        perror("msgget padre");
        exit(1);
    }

    if(pipe(pipe_d) == -1){ //apertura della pipe
        perror("pipe padre");
        exit(1);
    }

    //creazione processi figli...
    if(fork() == 0)
        reader(pipe_d, argv[2]); //il lettore deve avere il canale r/w della pipe
    if(fork() == 0){
        close(pipe_d[0]);
        close(pipe_d[1]);
        writer(coda_d);
    }

    close(pipe_d[1]); //chiusura del canale di scrittura della pipe per il padre
    messaggio.eof = 0;
    messaggio.type = 1;

    //lettura dalla pipe...
    _pipe = fdopen(pipe_d[0], "r"); //apro la pipe in un FILE*
    while(fgets(buffer, DIMBUF, _pipe)){
        if(strstr(buffer, argv[1]) != NULL){ //se la stringa argv[1] è contenuto in token...
            strcpy(messaggio.text, buffer);
            if(msgsnd(coda_d, &messaggio, sizeof(msg)-sizeof(long), 0) == -1){ //mando messaggio a writer
                perror("msgsnd padre");
                exit(1);
            }
        }
    }

    messaggio.eof = 1;
    if(msgsnd(coda_d, &messaggio, sizeof(msg)-sizeof(long), 0) == -1){ //mando eof a writer
        perror("msgsnd padre");
        exit(1);
    }

    //in chiusura...
    close(pipe_d[0]);
    wait(NULL);
    wait(NULL);
    msgctl(coda_d, IPC_RMID, NULL);
    printf("X [PADRE]: in chiusura...\n");
    exit(0);
}