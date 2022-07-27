/**
 * @file fgrep.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-27
 * 
 * Prova di laboratorio di SO del 2019-07-26
 * 
 * 12.02 - 12.46
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <wait.h>
#include <sys/types.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define DIMBUF 1024

typedef struct{
    long type; //tipo di ogni parola distinta
    unsigned eof;
    char parola[DIMBUF];
} padreMsg;

typedef struct{
    long type;
    unsigned eof;
    int numRiga;
    char riga[DIMBUF];
} figlioMsg;

void fileReader(int codaP, int codaF, char *pathFile, unsigned numParole){ //codaPF, codaFP, argv[file], numParole;
    padreMsg toReceive;
    figlioMsg toSend;
    FILE *f;

    if((f = fopen(pathFile, "r")) == NULL){ //apro il file su cui effettuare ricerca
        perror("fopen fileReader");
        fprintf(stderr, "fopen fileReader <%s>", pathFile);
        exit(1);
    }

    for(int i = 1; i <= numParole; i++){
        if((msgrcv(codaP, &toReceive, sizeof(padreMsg)-sizeof(long), i, 0)) == -1){ //ricevo gradualmente le parole distinte di tipo i-esimo
            fprintf(stderr, "msgrcv fileReader <%s>", pathFile);
            exit(1);
        }


    }
}

int main(int argc, char *argv[]){
    unsigned numFile = 0, numParole = 0;
    int codaPF_d, codaFP_d;
    padreMsg toSend;
    figlioMsg toReceive;

    if(argc < 4){
        fprintf("Uso: %s <parola-1> [parola-2] [...] @ <file-1> [file-2] [...]\n", argv[0]);
        exit(1);
    }

    for(int i = 1; i < argc; i++){
        if(strcmp(argv[i], "@") != 0)
            numParole++;
        else
            break;
    }
    numFile = (argc-2) - numParole; //... conto il numero di parole e per sottrazione il numero di file...

    if((codaPF_d = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione coda padre->figlio
        perror("msgget snd");
        exit(1);
    }
    if((codaFP_d = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione coda figlio->padre
        perror("msgget rcv");
        exit(1);
    }

    //creazione figli...
    for(int i = 0; i < numFile; i++)
        if(fork() == 0)
            fileReader(codaPF_d, codaFP_d, argv[i+(numParole+2)], numParole); //codaPF, codaFP, argv[file], numParole;

    toSend.eof = 0;

    for(int i = 1; i <= numParole; i++){ //mando tutte le parole a tutti i figli...
        strcpy(toSend.parola, argv[i]);
        toSend.type = i; //ogni parola ha un suo tipo

        for(int j = 0; j < numFile; j++){
            if((msgsnd(codaPF_d, &toSend, sizeof(padreMsg)-sizeof(long), 0)) == -1){
                perror("msgsnd padre");
                exit(1);
            }
        }
    }

    while(1){ //aspetto che i figli mi rispondano...

    }
}