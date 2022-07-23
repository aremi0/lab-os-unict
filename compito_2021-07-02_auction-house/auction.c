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
#include <string.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <time.h>

#define DIMBUF 2048
#define S_JUDGE 0 //sync
#define S_BIDDERS 1 //sync

typedef struct{
    unsigned eof;
    char description[DIMBUF];
    int minOffer;
    int maxOffer;
    int currentOffer;
    unsigned currentOfferID;
} shmMsg;

int WAIT(int sem_des, int numSem){
    struct sembuf op[1] = {{numSem, -1, 0}};
    return semop(sem_des, op, 1);
}
int SIGNAL(int sem_des, int numSem){
    struct sembuf op[1] = {{numSem, +1, 0}};
    return semop(sem_des, op, 1);
}

void bidders(int id, shmMsg *ptr, int sem){
    srand(time(NULL) % getpid());
    int counter = 1;

    while(1){
        WAIT(sem, S_JUDGE); //aspetto che il giudice rilasci la shm per lanciare la mia offerta

        if(ptr->eof)
            break;

        ptr->currentOffer = rand() % (ptr->maxOffer + 1);
        ptr->currentOfferID = id;
        printf("[B%u] invio offerta di %d EUR per asta n.%d\n", id, ptr->currentOffer, counter);

        SIGNAL(sem, S_BIDDERS); //dati elaborati, risveglio il judge...
        counter++;
    }

    //eof...
    shmdt(ptr);
    printf("\t[B%u] terminazione\n", id);
    exit(0);
}

int main(int argc, char *argv[]){
    int sem_d, shm_d, counter = 1;
    FILE *f;
    shmMsg *ptr;
    char buffer[DIMBUF], *token;
    int nBidders = atoi(argv[2]);

    int oldId = -1;
    int oldOffer = 0;
    unsigned validOffers = 0;


    if(argc != 3){
        fprintf(stderr, "Uso: %s <auction-file> <num-bidders>", argv[0]);
        exit(1);
    }
    if((f = fopen(argv[1], "r")) == NULL){ //apertura file input
        perror("fopen");
        exit(1);
    }
    if((sem_d = semget(IPC_PRIVATE, 2, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione vettore semafori
        perror("semget");
        exit(1);
    }
    if((semctl(sem_d, S_JUDGE, SETVAL, 0)) == -1){
        perror("semctl setval S_JUDGE");
        exit(1);
    }
    if((semctl(sem_d, S_BIDDERS, SETVAL, 0)) == -1){ //...inizializzazione default semafori
        perror("semctl setval S_JUDGE");
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

    //creazione figli...
    for(int i = 0; i < nBidders; i++)
        if(fork() == 0)
            bidders(i+1, ptr, sem_d); //id, *ptr, sem

    while(fgets(buffer, DIMBUF, f)){ //mentre leggo righe dal file input...
        token = strtok(buffer, ",");
        strcpy(ptr->description, token);
        token = strtok(NULL, ",");
        ptr->minOffer = atoi(token);
        token = strtok(NULL, ",");
        ptr->maxOffer = atoi(token); //...parsing

        printf("[J] lancio asta n.%d per %s con offerta minima di %d EUR e massima di %d EUR\n", counter, ptr->description, ptr->minOffer, ptr->maxOffer);

        oldId = -1;
        oldOffer = -1;
        validOffers = 0;
        ptr->eof = 0;

        for(int i = 0; i < nBidders; i++){
            SIGNAL(sem_d, S_JUDGE); //rilascio shm al primo offerente che arriva...
            WAIT(sem_d, S_BIDDERS); //aspetto che un bidders mi svegli...
            printf("[J] ricevuta offerta da B%u\n", ptr->currentOfferID);
            if((ptr->currentOffer >= ptr->minOffer) && (ptr->currentOffer <= ptr->maxOffer)){ //gestisco offerte valide e problema offerte valide uguali tra bidders...
                validOffers++;
                if(ptr->currentOffer > oldOffer){
                    oldOffer = ptr->currentOffer;
                    oldId = ptr->currentOfferID;
                }
            }
        }

        if(oldId == -1)
            printf("[J] l'asta n.%d per %s si è conclusa senza alcuna offerta valida pertanto l'oggetto non risulta assegnato\n\n", counter, ptr->description);
        else{
            printf("[J] l'asta n.%d per %s si è conclusa con %u offerte valide su %d;", counter, ptr->description, validOffers, nBidders);
            printf(" il vincitore è B%d che si aggiudica l'oggetto per %d EUR\n\n", oldId, oldOffer);
        }
        counter++;
    }

    ptr->eof = 1; //setto eof
    for(int i = 0; i < nBidders; i++) //e lo mando ai figli...
        SIGNAL(sem_d, S_JUDGE);

    fclose(f);
    shmdt(ptr);
    shmctl(shm_d, IPC_RMID, NULL);
    semctl(sem_d, 0, IPC_RMID, 0);
    printf("\t[J] terminazione\n");
    exit(0);
}