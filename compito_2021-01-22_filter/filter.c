/**
 * @file filter.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-25
 * 
 * Prova di laboratorio di SO del 2021-01-22
 * 
 * 3h3m
 */

#include <stdlib.h>
#include <stdio.h>
#include <wait.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <ctype.h>

#define MAX_LEN 1024
#define S_PADRE 0
#define S_FILTER 1

typedef struct{
    unsigned eof;
    char riga[MAX_LEN];
} shmMsg;

int WAIT(int sem_des, int numSem){
    struct sembuf op[1] = {{numSem, -1, 0}};
    return semop(sem_des, op, 1);
}
int SIGNAL(int sem_des, int numSem){
    struct sembuf op[1] = {{numSem, +1, 0}};
    return semop(sem_des, op, 1);
}

void upper(char *cmd, shmMsg *ptr){
    char *cursor;
    int i = 0;

    while((cursor = strstr(ptr->riga, cmd+1)) != NULL){ //se trova la sottostringa, cursor vi punterà
        i = 0;
        while(i < strlen(cmd+1)){ //applico il filtro upper per la lunghezza di 'parola'
            cursor[i] = toupper(cursor[i]);
            i++;
        }
    }
}

void lower(char *cmd, shmMsg *ptr){
    char *cursor;
    int i = 0;

    while((cursor = strstr(ptr->riga, cmd+1)) != NULL){ //se trova la sottostringa, cursor vi punterà
        i = 0;
        while(i < strlen(cmd+1)){ //applico il filtro lower per la lunghezza di 'parola'
            cursor[i] = tolower(cursor[i]);
            i++;
        }
    }
}

void replace(char *cmd, shmMsg *ptr){

}

void filter(int currFilter, int totFilter, char *cmd, shmMsg *ptr, int sem){ //currFilter, numFilter, argv[i+2], msg, sem_d
    while(1){
        WAIT(sem, S_FILTER);

        if(ptr->eof)
            break;
        
        //handling comando...
        if(cmd[0] == '^')
            upper(cmd, ptr);
        else if(cmd[0] == '_')
            lower(cmd, ptr);
        else
            replace(cmd, ptr);
        

        if(currFilter == totFilter) //sono l'ultimo filtro; sveglio il padre...
            SIGNAL(sem, S_PADRE);
        else //...altrimenti sveglio il filtro dopo di me e mi ri-addormento...
            SIGNAL(sem, S_FILTER);
    }

    //in chiusura...
    shmdt(ptr);
    printf("\t\t[F%d]: terminazione...\n", currFilter);
    exit(0);
}

int main(int argc, char *argv[]){
    int shm_d, sem_d, totFilter = argc-2;
    FILE *input;
    shmMsg *ptr;
    char buffer[MAX_LEN];

    if(argc < 3){
        fprintf(stderr, "Uso: %s <input-file> <filter-1> [filter-2] [...]\n", argv[0]);
        exit(1);
    }
    if((input = fopen(argv[1], "r")) == NULL){ //apertura file input in stream
        perror("fopen");
        exit(1);
    }
    if((shm_d = shmget(IPC_PRIVATE, sizeof(shmMsg), IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione shm
        perror("shmget");
        exit(1);
    }
    if((ptr = (shmMsg*)shmat(shm_d, NULL, 0)) == (shmMsg*)-1){ //attach shm
        perror("shmat");
        exit(1);
    }
    if((sem_d = semget(IPC_PRIVATE, 2, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione vettore semafori
        perror("semget");
        exit(1);
    }
    if((semctl(sem_d, S_PADRE, SETVAL, 0)) == -1){
        perror("semctl setval padre");
        exit(1);
    }
    if((semctl(sem_d, S_FILTER, SETVAL, 0)) == -1){ //...inizializzazione default semafori
    perror("semctl setval filter");
    exit(1);
    }

    ptr->eof = 0;

    //creazione figli...
    for(int i = 0; i < totFilter; i++) //grazie al 'for' creo i figli proceduralmente, chiameranno quindi la 'WAIT' in ordine di coda...
        if(fork() == 0)
            filter(i+1, totFilter, argv[i+2], ptr, sem_d); //currFilter, numFilter, argv[i+2], msg, sem_d

    while(fgets(ptr->riga, MAX_LEN, input)){ //mentre leggo righe dal file...

        SIGNAL(sem_d, S_FILTER);
        WAIT(sem_d, S_PADRE); //...deposito riga in shm, sveglio filter e aspetto che elabori...

        printf("[PADRE]: %s", ptr->riga);
    }

    ptr->eof = 1;
    for(int i = 0; i < totFilter; i++) //mando eof ai figli...
        SIGNAL(sem_d, S_FILTER);

    //in chiusura...
    fclose(input);
    shmdt(ptr);
    shmctl(shm_d, IPC_RMID, NULL);
    semctl(sem_d, 0, IPC_RMID, 0);
    printf("\t\t[PADRE]: terminazione...\n");
    exit(0);
}