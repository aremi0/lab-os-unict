/**
 * @file calculator.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-14
 * 
 * Prova di laboratorio di SO del 2022-07-07
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <wait.h>

/***
 *      segmento memoria condivisa:
 *  p[0] ==> long totale;
 *  p[1] ==> long valore;
 *  p[2] ==> long eof;
*/

#define S_MNG 0 //mutex
#define S_ADD 1 //sync
#define S_MUL 2 //sync
#define S_SUB 3 //sync

#define DIMBUF 256

int WAIT(int sem, int n){
    struct sembuf operazioni[1] = {{n, -1, 0}};
    return semop(sem, operazioni, 1);
}

int SIGNAL(int sem, int n){
    struct sembuf operazioni[1] = {{n, +1, 0}};
    return semop(sem, operazioni, 1);
}

void manager(long *shm, int sem, char *pathFile){
    FILE *file;
    char buffer[DIMBUF];
    long *totale, *valore, *eof;
    char aux[DIMBUF];

    //creo dei puntatori diretti alla shm per semplicità...
    totale = shm;
    valore = shm+1;
    eof = shm+2;

    *eof = *totale = 0; //inizializzo eof e totale a 0

    if((file = fopen(pathFile, "r")) == NULL){ //apertura file...
        perror("fopen manager");
        exit(1);
    }

    while(fgets(buffer, DIMBUF, file)){ //menter leggi le righe dal file...
        strncpy(aux, buffer+1, DIMBUF); //azzicco in aux i char dopo l'operazione...
        *valore = atol(aux);

        printf("[MNG]: risultato intermedio: %ld; letto: %s", *totale, buffer);

        if(buffer[0] == '+')
            SIGNAL(sem, S_ADD);
        else if(buffer[0] == '*')
            SIGNAL(sem, S_MUL);
        else if(buffer[0] == '-')
            SIGNAL(sem, S_SUB);

        WAIT(sem, S_MNG);
    }

    //in chiusura, ultima stamp, manda l'eof e exit(0)
    printf("[MNG]: risultato finale: %ld\n[MNG]: terminazione...\n", *totale);
    *eof = 1;
    SIGNAL(sem, S_ADD);
    SIGNAL(sem, S_MUL);
    SIGNAL(sem, S_SUB);


    fclose(file);
    exit(0);
}

void add(long *shm, int sem){
    long *totale, *valore, *eof;
    long tmpTot;
    //creo dei puntatori diretti alla shm per semplicità...
    totale = shm;
    valore = shm+1;
    eof = shm+2;

    while(1){
        WAIT(sem, S_ADD);
        if(*eof)
            break;

        tmpTot = *totale;
        *totale += *valore;
        printf("[ADD]: %ld+%ld=%ld\n", tmpTot, *valore, *totale);
        SIGNAL(sem, S_MNG);
    }

    //in chiusura...
    printf("[ADD]: terminazione...\n");
    exit(0);
}

void mul(long *shm, int sem){
    long *totale, *valore, *eof;
    long tmpTot;
    //creo dei puntatori diretti alla shm per semplicità...
    totale = shm;
    valore = shm+1;
    eof = shm+2;

    while(1){
        WAIT(sem, S_MUL);
        if(*eof)
            break;

        tmpTot = *totale;
        *totale *= *valore;
        printf("[MUL]: %ld*%ld=%ld\n", tmpTot, *valore, *totale);
        SIGNAL(sem, S_MNG);
    }

    //in chiusura...
    printf("[MUL]: terminazione...\n");
    exit(0);
}

void sub(long *shm, int sem){
    long *totale, *valore, *eof;
    long tmpTot;
    //creo dei puntatori diretti alla shm per semplicità...
    totale = shm;
    valore = shm+1;
    eof = shm+2;

    while(1){
        WAIT(sem, S_SUB);
        if(*eof)
            break;

        tmpTot = *totale;
        *totale -= *valore;
        printf("[SUB]: %ld-%ld=%ld\n", tmpTot, *valore, *totale);
        SIGNAL(sem, S_MNG);
    }

    //in chiusura...
    printf("[SUB]: terminazione...\n");
    exit(0);
}

int main(int argc, char *argv[]){
    int sem_d, shm_d;
    long *p;

    if(argc != 2){
        printf("Uso: %s\n", argv[0]);
        exit(1);
    }

    //creazione segmento memoria condivisa di size (3 long)
    if((shm_d = shmget(IPC_PRIVATE, 3 * sizeof(long), IPC_CREAT | IPC_EXCL | 0600)) == -1){
        perror("shmget");
        exit(1);
    }

    //attach memoria condivisa nel padre, tanto lo useranno tutti...
    if((p = (long *)shmat(shm_d, NULL, 0)) == (long *) -1){
        perror("shmat");
        exit(1);
    }

    //creazione vettore con 4 semafori
    if((sem_d = semget(IPC_PRIVATE, 4, IPC_CREAT | IPC_EXCL | 0600)) == -1){
        perror("semget");
        exit(1);
    }

    //inizializzazione default dei semafori
    if(semctl(sem_d, S_MNG, SETVAL, 0) == -1){
        perror("semctl setval S_MNG");
        exit(1);
    }
    if(semctl(sem_d, S_ADD, SETVAL, 0) == -1){
        perror("semctl setval S_ADD");
        exit(1);
    }
    if(semctl(sem_d, S_MUL, SETVAL, 0) == -1){
        perror("semctl setval S_MUL");
        exit(1);
    }
    if(semctl(sem_d, S_SUB, SETVAL, 0) == -1){
        perror("semctl setval S_SUB");
        exit(1);
    }

    //creazione processi figli
    if(fork() == 0)
        manager(p, sem_d, argv[1]);
    if(fork() == 0)
        add(p, sem_d);
    if(fork() == 0)
        mul(p, sem_d);
    if(fork() == 0)
        sub(p, sem_d); 

    //attesa terminazione...
    wait(NULL);
    wait(NULL);
    wait(NULL);
    wait(NULL);

    //in chiusura, pulizia...
    shmctl(shm_d, IPC_RMID, NULL);
    semctl(sem_d, 0, IPC_RMID, 0); //eliminazione vettore semafori
    printf("[P]: terminazione...\n");
    exit(0);
}