/**
 * @file parallel.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-16
 * 
 *  Prova di laboratorio di SO del 2021-12-10
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <wait.h>
#include <ctype.h>

#define DIMBUF 2048

//indice semafori
#define S_NEXT 0 //usato per sincronizzare processo Padre e Stampa
#define S_Li 1

int WAIT(int sem_d, int numSem){
    struct sembuf op[1] = {{numSem, -1, 0}};
    return semop(sem_d, op, 1);
}
int SIGNAL(int sem_d, int numSem){
    struct sembuf op[1] = {{numSem, +1, 0}};
    return semop(sem_d, op, 1);
}

/***
 *  SHM             ==> usata dai processi PADRE e LETTERA[i]
 *  |--- p[0]: char* text[DIMBUF];
 * 
*/
/***
 *  CODA            ==> usata dai processi LETTERA[i] e STAMPA
*/ 
typedef struct{
    long type; //associato a ciascun id dei processi L[i]
    int eof;
    int occorrenze;
} msg;

void lettera(int sem, char *shm, int coda, char id){
    msg messaggio;
    int nOccorrenze;

    messaggio.eof = 0;
    messaggio.type = (long)id;

    while(1){
        WAIT(sem, S_Li); //aspetto che il padre mi svegli...
        nOccorrenze = 0;

        if(shm[0] == '%') //uso '%' come carattere eof
            break;

        for(int i = 0; i < DIMBUF; i++)
            if(id == toupper(shm[i]))
                nOccorrenze++;

        messaggio.occorrenze = nOccorrenze;
        if((msgsnd(coda, &messaggio, sizeof(msg)-sizeof(long), 0)) == -1){
            perror("msgsnd lettera[i]");
            exit(1);
        }
    }

    //in chiusura...
    printf("\t\t[L.%c] terminazione...\n", id);
    exit(0);
}

void stampa(int sem, int coda){
    int myEof = 0;
    msg messaggio;
    int current[26], total[26], riga = 1;

    memset(&total, 0, sizeof(int)*26); //setto tutto totale con zeri

    while(1){
        for(int i = 0; i < 26; i++){ //ricevo i 26 messaggi dei L[i] prcessi...
            if((msgrcv(coda, &messaggio, sizeof(msg)-sizeof(long), 0, 0)) == -1){
                perror("msgrcv stampa");
                exit(1);
            }

            if(messaggio.eof){ //eof mandato dal padre...
                myEof = 1;
                break;
            }

            //"(int)((messaggio[i].type)-65)"  ||'A' = 65||==> 'A'-65 = 0     \/   'B'-65 = 1   ecc...
            current[(int)((messaggio.type)-65)] = messaggio.occorrenze;
            total[(int)((messaggio.type)-65)] += current[(int)((messaggio.type)-65)];
        }

        if(myEof)
            break;

        printf("[S]: riga n.%d: ", riga);
        for(int i = 0; i < 26; i++)
            printf("%c=%d ", (char)((int)'A'+i), current[i]);
        printf("\n");
        riga++;

        SIGNAL(sem, S_NEXT);
    }

    //in chiusura...
    printf("[S]: intero file: ");
    for(int i = 0; i < 26; i++)
        printf("%c=%d ", (char)((int)'A'+i), total[i]);
    printf("\n");

    printf("\t\t[S] terminazione...\n");
    exit(0);
}

int main(int argc, char *argv[]){
    msg eof;
    int sem_d, coda_d, shm_d, riga = 1;
    FILE *f;
    char buffer[DIMBUF];
    char *p;

    if(argc != 2){
        printf("Uso: %s\n", argv[0]);
        exit(1);
    }

    if((f = fopen(argv[1], "r")) == NULL){ //apertura del file in uno stream
        perror("fopen");
        exit(1);
    }

    if((shm_d = shmget(IPC_PRIVATE, sizeof(char) * DIMBUF, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione shm
        perror("shmget");
        exit(1);
    }
    if((p = (char*)shmat(shm_d, NULL, 0)) == (char*)-1){ //attach memoria condivisa
        perror("shmat");
        exit(1);
    }
    if((coda_d = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione coda
        perror("msgget");
        exit(1);
    }
    if((sem_d = semget(IPC_PRIVATE, 2, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione vettore semafori
        perror("semget");
        exit(1);
    }
    if((semctl(sem_d, S_NEXT, SETVAL, 0)) == -1){
        perror("semctl S_NEXT");
        exit(1);
    }
    if((semctl(sem_d, S_Li, SETVAL, 0)) == -1){ //...inizializzazione default semafori
        perror("semctl s_Li");
        exit(1);
    }

    //creazione figli...
    if(fork() == 0)
        stampa(sem_d, coda_d);
    for(int i = 0; i < 26; i++)
        if(fork() == 0)
            lettera(sem_d, p, coda_d, (char)((int)'A'+i)); //passo dei char


    while(fgets(buffer, DIMBUF, f)){
        printf("[P]: riga n.%d: %s", riga, buffer);
        strncpy(p, buffer, DIMBUF); //scrittura della riga nella shm
        for(int i = 0; i < 26; i++) //sveglio tutti i processo L[i]
            SIGNAL(sem_d, S_Li);
        WAIT(sem_d, S_NEXT); //aspetto il permesso del processo Stampa...
        riga++;
    }

    //in chiusura... mando eof e pulisco

    p[0] = '%'; //eof ai processi L[i]
    for(int i = 0; i < 26; i++) //sveglio tutti i processo L[i]
        SIGNAL(sem_d, S_Li);

    eof.eof = 1;
    eof.type = 1;
    if((msgsnd(coda_d, &eof, sizeof(msg)-sizeof(long), 0)) == -1){
        perror("msgsnd padre");
        exit(1);
    }

    for(int i = 0; i < 27; i++)
        wait(NULL);

    
    semctl(sem_d, 0, IPC_RMID, 0);
    shmctl(shm_d, IPC_RMID, NULL);
    msgctl(coda_d, IPC_RMID, NULL);

    printf("\t\t[PADRE] terminazione...\n");
    exit(0);
}