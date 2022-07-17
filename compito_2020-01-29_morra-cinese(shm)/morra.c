/**
 * @file morra.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-16
 * 
 * Prova di laboratorio di SO del 2020-01-29
 *   ||  - versione con shm.
 *   
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>

/***
 *  SHM
 *  |   p[0] = mossa_p1     (char)
 *  |   p[1] = mossa_p2     (char)
 *  |   p[2] = vincitore    (char)
 *  |   p[3] = eof          (char)
*/ 

#define S_GIUDICE 0
#define S_TABELLONE 1
#define S_PLAYER 2

int WAIT(int sem_d, int numSem){
    struct sembuf op[1] = {{numSem, -1, 0}};
    return semop(sem_d, op, 1);
}
int SIGNAL(int sem_d, int numSem){
    struct sembuf op[1] = {{numSem, +1, 0}};
    return semop(sem_d, op, 1);
}

char giocaMossa(){
    switch(rand()%3){
        case 0: return 's';
        case 1: return 'c';
        case 2: return 'f';
    }
}

void player(int sem, char *shm, char id){
    srand(time(NULL) % getpid()); //generazione seme rand
    char *mossaP1, *mossaP2, *eof; //puntatori diretti alla shm
    mossaP1 = shm;
    mossaP2 = shm+1;
    eof = shm+3;

    while(1){
        if(*eof == '1')
            break;

        if(id == '1'){ //i player giocano la loro mossa...
            *mossaP1 = giocaMossa();
            printf("[P%c] mossa '%c'\n", id, *mossaP1);
        }
        else{
            *mossaP2 = giocaMossa();
            printf("[P%c] mossa '%c'\n", id, *mossaP2);
        }

            SIGNAL(sem, S_GIUDICE); //segnalo al giudice...
            WAIT(sem, S_PLAYER); //...e aspetto il suo permesso per continuare
    }

    //in chiusura...
    printf("\t\t[P%c] terminazione...\n", id);
    exit(0);
}

char calcolaVincitore(char p1, char p2){
    switch(p1){
        case 'c':
            if(p2 == 'c') return 'e';
            else if(p2 == 'f') return '2';
            else return '1';
        case 'f':
            if(p2 == 'c') return '1';
            else if(p2 == 'f') return 'e';
            else return '2';
        case 's':
            if(p2 == 'c') return '2';
            else if(p2 == 'f') return '1';
            else return 'e';
    }
}

void tabellone(int sem, char *shm){
    int p1Win = 0, p2Win = 0;
    char *winner, *eof; //puntatori diretti alla shm
    winner = shm+2;
    eof = shm+3;

    while(1){
        WAIT(sem, S_TABELLONE); //aspetto che il giudice mi dia il permesso di stampare...

        if(*eof == '1')
            break;

        if(*winner == '1')
            p1Win++;
        else
            p2Win++;

        printf("[T] classifica temporanea: P1=%d P2=%d\n", p1Win, p2Win);
        SIGNAL(sem, S_GIUDICE); //... ho scritto, quindi risveglio il giudice...
    }

    //in chiusura...
    printf("[T] classifica finale: P1=%d P2=%d\n", p1Win, p2Win);
    printf("[T] vincitore del torneo: P%c\n", p1Win > p2Win ? '1' : '2');

    printf("\t\t[T] terminazione...\n");
    exit(0);
}

void giudice(int sem, char *shm, int totPartite){
    int currentPartita = 1;
    char *mossaP1, *mossaP2, *winner, *eof; //puntatori diretti alla shm
    mossaP1 = shm;
    mossaP2 = shm+1;
    winner = shm+2;
    eof = shm+3;
    *eof = '0'; //setto l'eof per sicurezza

    while(currentPartita <= totPartite){
        WAIT(sem, S_GIUDICE);
        WAIT(sem, S_GIUDICE); //...aspetto che i figli giochino le loro mosse

        if((*winner = calcolaVincitore(*mossaP1, *mossaP2)) == 'e'){ //patta... verrà ignorata
            printf("[G] partita n.%d patta, prossima...\n", currentPartita);
        }
        else{
            printf("[G] partita n.%d vinta da P%c\n", currentPartita,*winner);
            SIGNAL(sem, S_TABELLONE);
            WAIT(sem, S_GIUDICE); //aspetto che il tabellone scriva le sue cose
        }

        currentPartita++;
        SIGNAL(sem, S_PLAYER);
        SIGNAL(sem, S_PLAYER); //...risveglio i giocatori...
    }

    //in chiusura...
    *eof = '1';
    SIGNAL(sem, S_PLAYER);
    SIGNAL(sem, S_PLAYER);
    SIGNAL(sem, S_TABELLONE); //sveglio gli altri per fargli leggere l'eof

    printf("\t\t[GIUDICE] terminazione...\n");
    exit(0);
}

int main(int argc, char *argv[]){
    int sem_d, shm_d;
    char *p;

    if(argc != 2){
        printf("Uso: %s", argv[0]);
        exit(1);
    }

    if((shm_d = shmget(IPC_PRIVATE, sizeof(char) * 4, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creo segmento memoria condivisa
        perror("shmget");
        exit(1);
    }
    if((p = (char*)shmat(shm_d, NULL, 0)) == (char*)-1){ //attach memoria condivisa
        perror("shmat");
        exit(1);
    }

    if((sem_d = semget(IPC_PRIVATE, 4, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione semafori
        perror("semget");
        exit(1);
    }
    if((semctl(sem_d, S_GIUDICE, SETVAL, 0)) == -1){
        perror("semctl setval giudice");
        exit(1);
    }
    if((semctl(sem_d, S_TABELLONE, SETVAL, 0)) == -1){
        perror("semctl setval tabellone");
        exit(1);
    }
    if((semctl(sem_d, S_PLAYER, SETVAL, 0)) == -1){
        perror("semctl setval player");
        exit(1);
    }

    p[3] = '0'; //setto l'eof a FALSE

    //creazione figli...
    if(fork() == 0)
        giudice(sem_d, p, atoi(argv[1]));
    if(fork() == 0)
        tabellone(sem_d, p);
    if(fork() == 0)
        player(sem_d, p, '1');
    if(fork() == 0)
        player(sem_d, p, '2');

    wait(NULL);
    wait(NULL);
    wait(NULL);
    wait(NULL);

    //in chiusura...
    shmctl(shm_d, IPC_RMID, NULL);
    semctl(sem_d, 0, IPC_RMID, 0);
    printf("\t\t[PADRE] terminazione...\n");
    exit(0);
}