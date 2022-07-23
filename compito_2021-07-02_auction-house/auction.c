/**
 * @file auction.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-21
 * 
 * Prova di laboratorio di SO del 2021-07-02
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <wait.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <time.h>

#define DIMBUF 1024
#define MUTEX 0
#define S_BIDDERS 1

int WAIT(int sem_des, int numSem){
    struct sembuf op[1] = {{numSem, -1, 0}};
    return semop(sem_des, op, 1);
}
int SIGNAL(int sem_des, int numSem){
    struct sembuf op[1] = {{numSem, +1, 0}};
    return semop(sem_des, op, 1);
}

typedef struct{
    char description[DIMBUF];
    int minOffer;
    int maxOffer;
    int currentOffer;
    unsigned validCounter;
    unsigned idCurrOffer;
    unsigned eof;
}shmMsg;

void bidders(unsigned id, shmMsg *ptr, int sem){
    int counter = 1;
    srand(time(NULL) % getpid());

    while(1){
        WAIT(sem, MUTEX);

        if(ptr->eof)
            break;

        ptr->currentOffer = rand() % (ptr->maxOffer - ptr->minOffer + 100) + ptr->minOffer-100; //può generare offerte negative! {rand() % (max-min+1) + min}
        if(ptr->currentOffer >= ptr->minOffer && ptr->currentOffer <= ptr->maxOffer)
            ptr->validCounter++;
        ptr->idCurrOffer = id;
        printf("[B%u] invio offerta di %d per asta n.%d\n", id+1, ptr->currentOffer, counter);
        counter++;
        SIGNAL(sem, S_BIDDERS);
    }

    shmdt(ptr);
    printf("\t[B%u] terminazione\n", id);
    exit(0);
}

int main(int argc, char *argv[]){
    int shm_d, sem_d, counter = 1;
    shmMsg *ptr;
    FILE *input;
    char buffer[DIMBUF], *token;
    int nBidders = atoi(argv[2]);
    int firstAequoOffer[2];

    if(argc != 3){
        fprintf(stderr, "Uso %s <auction-file> <num-bidders>\n", argv[0]);
        exit(1);
    }
    if((input = fopen(argv[1], "r")) == NULL){ //apertuta file input
        perror("fopen");
        exit(1);
    }
    if((shm_d = shmget(IPC_PRIVATE, sizeof(shmMsg), IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione segmento condiviso
        perror("shmget");
        exit(1);
    }
    if((sem_d = semget(IPC_PRIVATE, 2, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione vettore semafori
        perror("semget");
        exit(1);
    }
    if((ptr = (shmMsg*)shmat(shm_d, NULL, 0)) == (shmMsg*)-1){ //attach segmento condiviso
        perror("shmat");
        exit(1);
    }
    if((semctl(sem_d, MUTEX, SETVAL, 1)) == -1){
        perror("semctl setval judge");
        exit(1);
    }
    if((semctl(sem_d, S_BIDDERS, SETVAL, 0)) == -1){ //...inizializzazione default semafori
        perror("semctl setval bidders");
        exit(1);
    }

    WAIT(sem_d, MUTEX); //primo ad avere mutua escluzione...

    for(int i = 0; i < nBidders; i++)
        if(fork() == 0)
            bidders(i, ptr, sem_d); //id, shm, sem

    while(fgets(buffer, DIMBUF, input)){ //mentre leggo righe dal file...
        token = strtok(buffer, ",");
        strcpy(ptr->description, token);
        token = strtok(NULL, ",");
        ptr->minOffer = atoi(token);
        token = strtok(NULL, ",");
        ptr->maxOffer = atoi(token);
        ptr->eof = 0;
        ptr->idCurrOffer = 0;
        ptr->validCounter = 0;
        firstAequoOffer[0] = 0;
        firstAequoOffer[1] = -1;

        printf("[J] lancio asta n.%d per %s con offerta minima di %d EUR e massima di %d EUR\n", counter, ptr->description, ptr->minOffer, ptr->maxOffer);

        for(int i = 0; i < nBidders; i++){
            SIGNAL(sem_d, MUTEX); //rilascio shm dopo aver avviato l'asta...
            WAIT(sem_d, S_BIDDERS); //aspetto che un bidders lanci la sua offerta
            printf("[J] ricevuta offerta da B%u\n", ptr->idCurrOffer+1);
            if((ptr->currentOffer > firstAequoOffer[0]) && (ptr->currentOffer <= ptr->maxOffer) && (ptr->currentOffer >= ptr->minOffer)){
                firstAequoOffer[0] = ptr->currentOffer;
                firstAequoOffer[1] = ptr->idCurrOffer;
            }
        }

        if(firstAequoOffer[1] == -1)
            printf("[J] l'asta n.%d per %s si è conclusa senza alcuna offerta valida pertanto l'oggetto non risulta assegnato\n\n", counter, ptr->description);
        else{
            printf("[J] l'asta n.%d per %s si è conclusa con %u valide su %d;", counter, ptr->description, ptr->validCounter, nBidders);
            printf(" il vincitore è B%d che si aggiudica l'oggeto per %d EUR\n\n", firstAequoOffer[1]+1, firstAequoOffer[0]);
        }

        counter++;
    }

    ptr->eof = 1;
    for(int i = 0; i < nBidders; i++)
        SIGNAL(sem_d, S_BIDDERS);

    shmdt(ptr);
    shmctl(shm_d, IPC_RMID, NULL);
    semctl(sem_d, 0, IPC_RMID, 0);
    fclose(input);
    printf("\t[J] terminazione\n");
    exit(0);
}