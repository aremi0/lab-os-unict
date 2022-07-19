/**
 * @file mydus.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-19
 * 
 * Prova di laboratorio di SO del 2021-07-26
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <wait.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <dirent.h>

#define MAX_PATH_LEN 512

#define S_bho 0
#define S_bho1 1

/***
 *      ||SHM table:    (char *)
 *  | p[0]
 *  | p
*/

typedef struct{
    long type;
    char text[MAX_PATH_LEN];
} msg;

int WAIT(int sem_des, int semNum){
    struct sembuf op[1] = {{semNum, -1, 0}};
    return semop(sem_des, op, 1);
}

int SIGNAL(int sem_des, int semNum){
    struct sembuf op[1] = {{semNum, +1, 0}};
    return semop(sem_des, op, 1);
}

int main(int argc, char *argv[]){
    int shm_d, sem_d, coda_d;

    if(argc < 2){
        printf("Uso: %s <path-1> <path-2> ...\n", argv[0]);
        exit(1);
    }

    if((shm_d = shmget(IPC_PRIVATE, sizeof(char) * MAX_PATH_LEN, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione segmento condiviso
        perror("shmget");
        exit(1);
    }
    if((coda_d = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione coda
        perror("shmat");
        exit(1);
    }
    if((sem_d = semget(IPC_PRIVATE, 2, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione vettore semafori
        perror("semget");
        exit(1);
    }
    if((semctl(sem_d, S_bho, SETVAL, 0)) == 1){
        perror("semctl setval bho");
        exit(1);
    }
    if((semctl(sem_d, S_bho1, SETVAL, 0)) == 1){ //...inizializzazione default semafori
        perror("semctl setval bho1");
        exit(1);
    }

    //creazione figli...
    if(fork() == 0)
        stater(p, sem);
    for(int i = 1; i < argc; i++)
        if(fork() == 0)
            scanner(p, sem, argv[i]);

    for(int i = 0; i < argc; i++)
        wait(NULL);

/*    //in chiusura...
    shmctl(shm_d, IPC_RMID, NULL);
    semctl(sem_d, 0, IPC_RMID, 0);

    printf("\t\t[PADRE] terminazione...\n");
    exit(0); */
}