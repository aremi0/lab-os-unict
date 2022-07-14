/**
 * @file sort.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-14
 * 
 *  Prova di laboratorio di SO del 2022-02-18
 * 
 *  N.b. e' lento come la merda, quindi per ricevere l'output devi aspettare
 *  una 20ina di secondi, però funziona
 */

/***
 *      Codice insertion-sort default
 * 
 *  for(int i = 1, j; i < n; i++){
 *      temp = a[i];
 *      j = i-1;
 * 
 *      while(j >= 0 && temp <= a[j]){
 *          a[j+1] = a[j];
 *          j--
 *      }
 *      
 *      a[j+1] = temp;
 *  }
 * 
*/ 

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MAX_WORD_LEN 50

typedef struct{
    long type; //type 1 => comparer || type 2 => parser || type 3 => PADRE
    int eof;
    int res;
    char p1[MAX_WORD_LEN];
    char p2[MAX_WORD_LEN];
} msg;

int contaRighe(char *path){
    FILE *file;
    char riga[MAX_WORD_LEN];
    int counter = 0;

    if((file = fopen(path, "r")) == NULL){ //apertura del file...
        perror("fopen sorter");
        exit(1);
    }

    while(fgets(riga, MAX_WORD_LEN, file)) //mentre leggi ogni riga del file...
        counter++;

    fclose(file);
    return counter;
}

void sorter(int coda, char *filePath){
    FILE *file;
    msg messaggio;
    int nRighe = contaRighe(filePath); //scansiono preliminarmente il file e conto le righe, come suggerito...
    char **allWords = malloc(nRighe * sizeof(char*)); //alloco dinamicamente un vettore di stringhe
    char buffer[MAX_WORD_LEN];

    if((file = fopen(filePath, "r")) == NULL){ //apertura del file...
        perror("fopen sorter");
        exit(1);
    }

    memset(&messaggio, 0, sizeof(msg));
    messaggio.eof = 0;

    //riempio il vettore dinamico allWords[] come suggerito...
    for(int i = 0; i < nRighe; i++){ //legge il nome, elimina il '\n' e lo salva alla i-esima pos di allWords[]...
        fgets(buffer, MAX_WORD_LEN, file);

        if(buffer[strlen(buffer)-1] == '\n')
            buffer[strlen(buffer)-1] = '\0';

        allWords[i] = malloc(MAX_WORD_LEN * sizeof(char));
        strncpy(allWords[i], buffer, MAX_WORD_LEN);
    }

    //insertion-sort
    for(int i = 1, j; i < nRighe; i++){
        strcpy(buffer, allWords[i]);
        j = i-1;

        while(j >= 0){
            strcpy(messaggio.p1, buffer); //p1
            strcpy(messaggio.p2, allWords[j]); //p2
            messaggio.type = 1;

            if((msgsnd(coda, &messaggio, sizeof(msg)-sizeof(long), 0)) == -1){ //mando richiesta
                perror("msgsnd sorter");
                exit(1);
            }

            if((msgrcv(coda, &messaggio, sizeof(msg)-sizeof(long), 2, 0)) == -1){ //aspetto risposta
                perror("msgrcv sorter");
                exit(1);
            }

            if(messaggio.res){
                strcpy(allWords[j+1], allWords[j]);
                j--;
            }
            else
                break;
        }
        strcpy(allWords[j+1], buffer);
    }

    //vettore ordinato, mando eof a comparer
    memset(&messaggio, 0, sizeof(msg));
    messaggio.eof = 1;
    messaggio.type = 1;
    if((msgsnd(coda, &messaggio, sizeof(msg)-sizeof(long), 0)) == -1){
        perror("msgsnd sorter");
        exit(1);
    }

    //vettore ordinato, procedo con l'invio al padre per la stampa...
    messaggio.eof = 0;
    messaggio.type = 3;
    for(int i = 0; i < nRighe; i++){
        strcpy(messaggio.p1, allWords[i]);
        if((msgsnd(coda, &messaggio, sizeof(msg)-sizeof(long), 0)) == -1){ //mando richiesta al PADRE
            perror("msgsnd sorter");
            exit(1);
        }
    }

    //mando eof al padre...
    messaggio.eof = 1;
    if((msgsnd(coda, &messaggio, sizeof(msg)-sizeof(long), 0)) == -1){ //mando richiesta al PADRE
        perror("msgsnd sorter");
        exit(1);
    }

    free(allWords);
    fclose(file);
    printf("\t\t[PARSER]: terminazione...\n");
    exit(0);
}

void comparer(int coda){
    msg messaggio;

    while(1){
        memset(&messaggio, 0, sizeof(msg));

        if(msgrcv(coda, &messaggio, sizeof(msg)-sizeof(long), 1, 0) == -1){
            perror("msgrcv comparer");
            exit(1);
        }

        if(messaggio.eof)
            break;
        
        messaggio.type = 2;
        messaggio.res = strcasecmp(messaggio.p1, messaggio.p2) <= 0 ? 1 : 0;

        if(msgsnd(coda, &messaggio, sizeof(msg)-sizeof(long), 0) == -1){
            perror("msgsnd comparer");
            exit(1);
        }
    }

    printf("\t\t[COMPARER]: terminazione...\n");
    exit(0);
}

int main(int argc, char *argv[]){
    int coda_d;
    msg messaggio;

    if(argc != 2){
        printf("Uso: %s", argv[0]);
        exit(1);
    }

    //creazione coda messaggi
    if((coda_d = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){
        perror("msgget");
        exit(1);
    }

    if(fork() == 0)
        sorter(coda_d, argv[1]);
    if(fork() == 0)
        comparer(coda_d);

    while(1){
        if(msgrcv(coda_d, &messaggio, sizeof(msg)-sizeof(long), 3, 0) == -1){
            perror("msgrcv comparer");
            exit(1);
        }

        if(messaggio.eof)
            break;

        printf("%s\n", messaggio.p1);
    }


    //in chiusura...
    msgctl(coda_d, IPC_RMID, NULL);
    printf("\t\t[PADRE]: terminazione...\n");
    exit(0);
}