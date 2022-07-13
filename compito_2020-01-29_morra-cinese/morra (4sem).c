/**
 * @file morra.c
 * @author aremi
 * @brief 
 * @version 0.1
 * @date 2022-06-13
 * 
 * @copyright Copyright (c) 2022
 * 
 * - 4 processi: player1, player2, giudice e tabellone.     (3 semafori?)
 * - I primi 3 comunicano tramite shm                       (shm)
 * - giudice e tabellone comunicano tramite coda FIFO       (coda FIFO -> struttura msg)
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <wait.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <time.h>

#define T_SEM 0
#define G_SEM 1
#define P1_SEM 2
#define P2_SEM 3

typedef struct{ //Struttura per inviare messaggi nella coda FIFO
    long mtype;
    char winner;
    uint eof;
} msg;

typedef struct{
    char mossaP1;
    char mossaP2;
    char winner;
    uint eof;
} gameStatus;

int WAIT(int semId, int semNum){
    struct sembuf op[1] = {{semNum, -1, 0}};
    return semop(semId, op, 1);
}

int SIGNAL(int semId, int semNum){
    struct sembuf op[1] = {{semNum, +1, 0}};
    return semop(semId, op, 1);
}

char giocaMossa(){
    int aux = rand() % 3;
    switch(aux){
        case 0: return 'f';
        case 1: return 'c';
        case 2: return 's';
    }
}

char calcolaVincitore(char p1, char p2){
    switch(p1){
        case 'c':
            if(p2 == 's') return '1';
            if(p2 == 'f') return '2';
            return 'p';
        case 'f':
            if(p2 == 's') return '2';
            if(p2 == 'f') return 'p';
            return '1';
        case 's':
            if(p2 == 's') return 'p';
            if(p2 == 'f') return '1';
            return '2';
    }
}

void player(char id, gameStatus *shm, int sem){
    srand(getpid());

    while(1){
        if(shm->eof == 1) //il match è finito e devo sloggiare...
            exit(0);

        //gioco la mossa e la scrivo
        if(id == '1'){
            shm->mossaP1 = giocaMossa();
            printf("[P1]: ho giocato: %c\n", shm->mossaP1);
            SIGNAL(sem, G_SEM);
            WAIT(sem, P1_SEM);
        }
        else{
            shm->mossaP2 = giocaMossa();
            printf("[P2]: ho giocato: %c\n", shm->mossaP2);
            SIGNAL(sem, G_SEM);
            WAIT(sem, P2_SEM);
        }
    }
}

void giudice(int nPartite, gameStatus *shm, int coda, int sem){
    msg messaggio; //...per comunicare con il tabellone
    char winner;

    messaggio.eof = 0;
    messaggio.mtype = 1;

    for(int i = 1; i <= nPartite; i++){
        printf("[G]: in attesa della mossa dei giocatori...\n");
        WAIT(sem, G_SEM);
        WAIT(sem, G_SEM);

        //a questo punto i due processi player hanno mandato la SIGNAL al semG
        printf("[G]: partita n.{%d}\n", i);
        shm->winner = calcolaVincitore(shm->mossaP1, shm->mossaP2);

        if(shm->winner == 'p'){ //...se è patta ripeti la partita
            printf("[G]: la partita n.{%d} è risultata PATTA e da rigiocare!\n", i);
            printf("[G]: -----P1{%c} vs P2{%c}-----\n", shm->mossaP1, shm->mossaP2);
            i--;
        } else{ //... se non è patta invio il risultato al tabellone

            messaggio.winner = shm->winner;
            if(msgsnd(coda, &messaggio, sizeof(messaggio), 0) == -1){ //invio il messaggio...
                perror("msgsnd giudice");
                exit(1);
            }
            printf("[G]: -----P1{%c} vs P2{%c}-----\n", shm->mossaP1, shm->mossaP2);
            SIGNAL(sem, T_SEM); //segnalo il tabellone per scrivere il risultato...
            WAIT(sem, G_SEM); //... e mi addormento per aspettare la sua SIGNAL dopo la sua scrittura
        }


        if(i == nPartite){ //purtroppo devo fare sta vaccata
            //se questa è stata l'ultima partita devo far uscire a tutti
            shm->eof = 1;
            messaggio.eof = 1;

            if(msgsnd(coda, &messaggio, sizeof(messaggio), 0) == -1){ //invio il messaggio...
                perror("msgsnd2 giudice");
                exit(1);
            }           

            SIGNAL(sem, P1_SEM);
            SIGNAL(sem, P2_SEM);
            SIGNAL(sem, T_SEM);
            exit(0);
        }

        SIGNAL(sem, P1_SEM); //...altrimenti risveglio i giocatori
        SIGNAL(sem, P2_SEM);
    }

}

void tabellone(int coda, int sem){
    msg messaggio;
    int p1w = 0, p2w = 0;

    while(1){
        WAIT(sem, T_SEM); //aspetto che mi sveglino
        
        if(msgrcv(coda, &messaggio, sizeof(msg), 0, SHM_RDONLY) == -1){ //leggo il messaggio
            perror("msgrcv tabellone");
            exit(1);
        }

        if(messaggio.eof == 0){ //ancora non è finito il match...
            printf("[T]: il vincitore della partita è {P%c}!\n", messaggio.winner);

            if(messaggio.winner == '1') //tengo traccia delle vincite totali
                p1w++;
            else
                p2w++;

        } else{
            //la partita è finita, segnalo il vincitore assoluto e me ne vado
            printf("[T]: il match è terminato! Il vincitore è {P%c}!\n", p1w > p2w ? '1' : '2');
            printf("[T]: P1{%d} vs P2{%d}\n", p1w, p2w);
            exit(0);
        }

        SIGNAL(sem, G_SEM); //segnalo il giudice che ho fatto...(e mi addormento al prossimo ciclo).
    }
}

int main(int argc, char *argv[]){
    int nPartite, dsCoda, semId, shmId;
    gameStatus *ptr;

    //controlli
    if(argc < 2){
        printf("Uso: %s <numero-partite>\n", argv[0]);
        exit(1);
    }
    if((nPartite = atoi(argv[1])) < 1){
        printf("Inserire un numero di partite maggiore di zero!\n");
        exit(1);
    }

    //creazione coda FIFO
    if((dsCoda = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){
        perror("msgget");
        exit(1);
    }

    //creazione e connessione a segmento memoria condiviso e azzeramento
    if((shmId = shmget(IPC_PRIVATE, sizeof(gameStatus), IPC_CREAT | IPC_EXCL | 0600)) == -1){
        perror("shmget");
        exit(1);
    }
    if((ptr = (gameStatus *)shmat(shmId, NULL, 0)) == (gameStatus *) -1){
        perror("shmat");
        exit(1);
    }
    memset(ptr, 0, sizeof(gameStatus));

    //creazione e inizializzazione semafori
    if((semId = semget(IPC_PRIVATE, 4, IPC_CREAT | IPC_EXCL | 0600)) == -1){
        perror("semget");
        exit(1);
    }
    if(semctl(semId, G_SEM, SETVAL, 0) == -1){
        perror("semG set val");
        exit(1);
    }
    if(semctl(semId, T_SEM, SETVAL, 0) == -1){
        perror("semT set val");
        exit(1);
    }
    if(semctl(semId, P1_SEM, SETVAL, 0) == -1){
        perror("semP set val");
        exit(1);
    }
    if(semctl(semId, P2_SEM, SETVAL, 0) == -1){
        perror("semP set val");
        exit(1);
    }

    //creazione processi
    if(fork() != 0){
        if(fork() != 0){
            if(fork() != 0){
                if(fork() != 0){
                    //corpo del padre
                    wait(NULL);
                    wait(NULL);
                    wait(NULL);
                    wait(NULL);
                } else{
                    player('1', ptr, semId);
                }
            } else{
                player('2', ptr, semId);
            }
        } else{
            tabellone(dsCoda, semId);
        }
    } else{
        giudice(nPartite, ptr, dsCoda, semId);
    }

    //pulizia lasciata al padre
    msgctl(dsCoda, IPC_RMID, NULL); //coda FIFO
    shmctl(shmId, IPC_RMID, NULL); //segmento shm
    semctl(semId, 0, IPC_RMID, 0); //semafori
    exit(0);
}