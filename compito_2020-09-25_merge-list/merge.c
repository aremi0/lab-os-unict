/**
 * @file merge.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-24
 * 
 * Prova di laboratorio di SO del 2020-09-25
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <ctype.h>
#include <fcntl.h>

#define RIGHE 100
#define DIMBUF 32

typedef struct{
    long type; //ci azzicco l'id del reader mittente (1 o 2)
    unsigned eof;
    char text[DIMBUF];
}codaMsg;

void reader(unsigned id, char *path, int coda_d){ //id, argv[], coda_d
    FILE *input;
    char buffer[DIMBUF];
    codaMsg msg;
    int initSpace, finalSpace;

    msg.eof = 0;
    msg.type = id;

    if((input = fopen(path, "r")) == NULL){ //apertura file input in lettura
        perror("fopen");
        exit(1);
    }

    while(fgets(buffer, DIMBUF, input)){ //mentre leggo righe...
        initSpace = 0;
        finalSpace = 0;

        for(int i = 0, j = strlen(buffer); i <= j; i++, j--){ //conto gli spazi e '\n' ad inizio e fine stringa...
            if((i == j) && (isspace(buffer[j]) != 0)) //per le stringhe dispari...
                finalSpace++;
            if(isspace(buffer[i]) != 0)
                initSpace++;
            if(isspace(buffer[j]) != 0)
                finalSpace++;
        }

        buffer[strlen(buffer) - finalSpace] = '\0';
        strcpy(msg.text, buffer+initSpace);

        printf("[R%u] mando '%s'\n", id, msg.text);

        if((msgsnd(coda_d, &msg, sizeof(codaMsg)-sizeof(long), 0)) == -1){
            perror("msgsnd reader");
            exit(1);
        }
    }

    msg.eof = 1;
    if((msgsnd(coda_d, &msg, sizeof(codaMsg)-sizeof(long), 0)) == -1){ //mando eof al padre...
        perror("msgsnd reader");
        exit(1);
    }

    fclose(input);
    printf("\t[R%u] terminazione...\n", id);
    exit(0);
}

void writer(int *pipe_d){
    char buffer[DIMBUF];
    int r = 0;
    close(pipe_d[1]);

    while((r = read(pipe_d[0], buffer, DIMBUF)) > 0){
        printf("[W] ricevuto: '%s'\n", buffer);
    }

    close(pipe_d[0]);
    printf("\t[W] terminazione...\n");
    exit(0);
}

int main(int argc, char *argv[]){
    int pipe_d[2], coda_d, msgCounter = 0, dupCounter, eofCounter = 0;
    codaMsg msg;
    char words[RIGHE*2][DIMBUF];

    if(argc != 3){
        fprintf(stderr, "Uso: %s <file-1.txt> <file-2.txt>\n", argv[0]);
        exit(1);
    }
    if((pipe(pipe_d)) == -1){ //creazione pipe
        perror("pipe");
        exit(1);
    }
    if((coda_d = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione coda
        perror("msgget");
        exit(1);
    }

    //creazione figli...
    if(fork() == 0)
        reader(1, argv[1], coda_d); //id, argv[], coda_d ==> READER-1
    if(fork() == 0)
        reader(2, argv[2], coda_d); //id, argv[], coda_d ==> READER-2
    if(fork() == 0)
        writer(pipe_d); //pipe_d in lettura

    while(1){
        if((msgrcv(coda_d, &msg, sizeof(codaMsg)-sizeof(long), 0, 0)) == -1){
            perror("msgrcv padre");
            exit(1);
        }

        if(msg.eof){
            eofCounter++;

            if(eofCounter == 2)
                break;

            continue;
        }

        dupCounter = 1;
        strcpy(words[msgCounter], msg.text);
        msgCounter++;

        for(int i = 0; i < msgCounter-1; i++) //cerco se la stringa arrivata era già presente...
            if(strcasecmp(words[i], msg.text) == 0){
                dupCounter++;
                break;
            }

        if(dupCounter > 1) //la parola è un duplicato...
            printf("[P] la parola '%s' mandata da W%ld è un duplicato\n", msg.text, msg.type);
        else
           write(pipe_d[1], msg.text, DIMBUF); //altrimenti la mando alla pipe
    }

    //ricevute eof da entrambi i reader
    //devo mandare eof a writer sulla pipe
    //write(pipe_d[1], (void*)-1, DIMBUF); //eof a writer
    close(pipe_d[0]);
    close(pipe_d[1]);
    msgctl(coda_d, IPC_RMID, NULL);
    printf("\t[PADRE] terminazione...\n");
    exit(0);
}