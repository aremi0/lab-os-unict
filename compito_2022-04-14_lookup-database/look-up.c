/**
 * @file look-up.c
 * @author are
 * @version 0.1
 * @date 2022-07-13
 * 
 *  Prova di laboratorio di so del 2022-04-14
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <wait.h>

#define DIMBUF 512

/***
 *  ----msg type:
 *  type 0: all==>database
 *  type 1: processo1
 *  type 2: processo2
*/

typedef struct{
    long type; //processo mittente
    char eof;
    char nome[DIMBUF];
} queryMsg;

typedef struct{
    long type; //processo processo
    char eof;
    int valore;
} outMsg;

typedef struct{ //struttura di supporto per il db (per caricarlo in ram)
    char nome[DIMBUF];
    int valore;
} entryDb;

void input(int coda, char *pathInput, long id){
    FILE *input;
    queryMsg query;
    char nome[DIMBUF];
    int i = 1;

    //inizializzazione default 'query'
    query.eof = 0;
    query.type = id; //processo mittente

    if((input = fopen(pathInput, "r")) == NULL){ //apertura file input
        perror("fopen processo");
        exit(1);
    }

    while(fgets(nome, DIMBUF, input)){ //mentre leggo righe...
        nome[strlen(nome)-1] = '\0';
        strcpy(query.nome, nome);
        if((msgsnd(coda, &query, sizeof(queryMsg)-sizeof(long), 0)) == -1){
            perror("msgsnd processo");
            exit(1);
        }

        printf("[IN%ld]: inviata query n.%d '%s'\n", id, i, query.nome);
        i++;
    }
    
    //finito di leggere i nomi... in chiusura
    query.eof = 1;
    if((msgsnd(coda, &query, sizeof(queryMsg)-sizeof(long), 0)) == -1){ //eof
        perror("msgsnd processo");
        exit(1);
    }

    fclose(input);
    exit(0);
}

int contaRighe(char *pathFile){
    FILE *file;
    char entry[DIMBUF];
    int counter = 0;

    if((file = fopen(pathFile, "r")) == NULL){
        perror("fopen database contaRighe");
        exit(1);
    }

    while(fgets(entry, DIMBUF, file))
        counter++;

    fclose(file);
    return counter;
}

entryDb parsing(char *riga){
    char *nome;
    char *valore;
    entryDb entry;

    nome = strtok(riga, ":");
    valore = strtok(NULL, ":");
    strcpy(entry.nome, nome);
    entry.valore = atoi(valore);

    return entry;
}

/***
 * Carica il file database in memoria centrale parsificandolo in un entry[];
*/
entryDb* loadDatabase(char* path, int nRighe){
    FILE *file;
    char riga[DIMBUF];
    entryDb *entry = malloc(sizeof(entryDb) * nRighe); //caricamento in ram

    if((file = fopen(path, "r")) == NULL){
        perror("fopen database contaRighe");
        exit(1);
    }

    for(int i = 0; i < nRighe; i++){
        fgets(riga, DIMBUF, file);
        entry[i] = parsing(riga); //riempio entry parsificando le righe
    }

    fclose(file);
    return entry;
}

int searchEntry(entryDb *database, char *nome, int nRighe){
    for(int i = 0; i < nRighe; i++){
        if(strcmp(database[i].nome, nome) == 0)
            return i;
    }

    return -1;
}

void database(int codaRequest, int codaReponse, char *pathDb){
    queryMsg query;
    outMsg out;
    entryDb* database; //per allocare il file database in ram (malloc); vettore che conterrà tutte le voci del db parsificate
    int nRighe;
    int _exit = 0;
    int indexEntry;

    nRighe = contaRighe(pathDb); //pre-lettura del file e conteggio righe
    printf("\t\t[DB]: letti %d record dal file <%s>\n", nRighe, pathDb);
    database = loadDatabase(pathDb, nRighe); //caricamento in ram

    while(1){ //ricezione query dai processi
        if((msgrcv(codaRequest, &query, sizeof(queryMsg)-sizeof(long), 0, 0)) == -1){
            perror("msgrcv database");
            exit(1);
        }

        if(query.eof) //devo mandare eof a processo 'out', chiudere *fileDb e exit(0)
            _exit++;
        if(_exit == 2)
            break;

        if((indexEntry = searchEntry(database, query.nome, nRighe)) == -1){ //ritorna l'indice del match (o -1)
            printf("X\t\t[DB]: query '%s' da IN%ld non trovata\n", query.nome, query.type);
            continue;
        }

        //ha trovato un match...
        printf("V\t\t[DB]: query '%s' da IN%ld trovata con valore %d\n", database[indexEntry].nome, query.type, database[indexEntry].valore);
        out.eof = 0;
        out.type = query.type;
        out.valore = database[indexEntry].valore;

        if((msgsnd(codaReponse, &out, sizeof(outMsg)-sizeof(long), 0)) == -1){
            perror("msgsnd database");
            exit(1);
        }
    }

    out.eof = 1;
    if((msgsnd(codaReponse, &out, sizeof(outMsg)-sizeof(long), 0)) == -1){ //eof al processo 'out'
        perror("msgsnd database");
        exit(1);
    }

    //in chiusura...
    free(database);
    exit(0);
}

int output(int codaReponse){
    int tot1 = 0, tot2 = 0;
    int c1 = 0, c2 = 0;
    outMsg messaggio;

    while(1){
        if((msgrcv(codaReponse, &messaggio, sizeof(outMsg)-sizeof(long), 0, 0)) == -1){
            perror("msgrcv output");
            exit(1);
        }

        if(messaggio.eof)
            break;

        if(messaggio.type == 1){
            tot1 += messaggio.valore;
            c1++;
        }
        else{
            tot2 += messaggio.valore;
            c2++;
        }
    }

    //in chiusura, ultime stampe ed exit()
    printf("[OUT]: ricevuti n.%d valori validi per IN1 con totale %d\n", c1, tot1);
    printf("[OUT]: ricevuti n.%d valori validi per IN2 con totale %d\n", c2, tot2);
    exit(0);
}

int main(int argc, char *argv[]){
    int codaRequest, codaReponse;

    if(argc < 4){
        printf("Uso: ./lookup <database.txt> <input1.txt> <intput2.txt>\n");
        exit(1);
    }

    //creo le code per l'IPC
    if((codaRequest = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){
        perror("msgget");
        exit(1);
    }
    if((codaReponse = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){
        perror("msgget");
        exit(1);
    }

    //creazione dei processi figli
    if(fork() == 0)
        database(codaRequest, codaReponse, argv[1]);
    if(fork() == 0)
        input(codaRequest, argv[2], 1);
    if(fork() == 0)
        input(codaRequest, argv[3], 2);
    if(fork() == 0)
        output(codaReponse);

    wait(NULL);
    wait(NULL);
    wait(NULL);
    wait(NULL);

    //pulizia e exit() del padre
    msgctl(codaRequest, IPC_RMID, NULL);
    msgctl(codaReponse, IPC_RMID, NULL);
    exit(0);
}