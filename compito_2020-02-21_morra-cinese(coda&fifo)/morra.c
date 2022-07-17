/**
 * @file morra.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-15
 * 
 * Prova di laboratorio di SO del 2020-02-21
 *  ||  versione con coda e fifo.
 *  ||  in questa versione le partite patte vengono reiterate.
 * 
 *  ||  FIFO;
 *  |----scrittura: con write() aperta in un 'int fifo_d'.
 *  |----lettura:   con fgetc() aperta in uno stream 'FILE* fifo'
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/sem.h>
#include <wait.h>
#include <time.h>

#define S_GIUDICE 0
#define S_TABELLONE 1

int WAIT(int semDes, int nSem){
    struct sembuf op[1] = {{nSem, -1, 0}};
    return semop(semDes, op, 1);
}
int SIGNAL(int semDes, int nSem){
    struct sembuf op[1] = {{nSem, +1, 0}};
    return semop(semDes, op, 1);
}
/***
 *  semafori per sincronizzare Giudice e Tabellone;
 * 
 *  type 1 ==> Giudice;
 *  type 2 ==> p1 e p2 (sfruttato per mandare l'eof);
*/ 
typedef struct{
    long type;
    int eof;
    char mossa;
    char player;
} msg;

void tabellone(char *pathFifo, int sem){
    FILE *fifo;
    char winner;
    int p1Tmp = 0, p2Tmp = 0;

    if((fifo = fopen(pathFifo, "r")) == NULL){ //apertura della fifo
        perror("open giudice");
        exit(1);
    }

    while(1){
        WAIT(sem, S_TABELLONE); //aspetto che il giudice scriva sulla mia fifo...
        winner = (char)fgetc(fifo); //leggo 1byte dalla fifo...

        if(winner == '1'){
            p1Tmp++;
            printf("[T] classifica temporanea: P1=%d P2=%d\n\n", p1Tmp, p2Tmp);
        }
        else if(winner == '2'){
            p2Tmp++;
            printf("[T] classifica temporanea: P1=%d P2=%d\n\n", p1Tmp, p2Tmp);
        }
        else
            break;

        SIGNAL(sem, S_GIUDICE);
    }

    printf("[T] classifica finale: P1=%d P2=%d\n", p1Tmp, p2Tmp);
    winner = p1Tmp > p2Tmp ? '1' : '2';
    printf("[T] vincitore del torneo: P%c\n", winner);
    SIGNAL(sem, S_GIUDICE);

    //in chiusura...
    fclose(fifo);
    printf("\t\t[T] terminazione...\n");
    exit(0);
}

char calcolaVincitore(msg m1, msg m2){
    switch(m1.mossa){
        case 's':
            if(m2.mossa == 's') return 'e';
            else if(m2.mossa == 'c') return m2.player;
            else return m1.player;
        case 'c':
            if(m2.mossa == 's') return m1.player;
            else if(m2.mossa == 'c') return 'e';
            else return m2.player;
        case 'f':
            if(m2.mossa == 's') return m2.player;
            else if(m2.mossa == 'c') return m1.player;
            else return 'e';
    }
}

void giudice(int coda, char *pathFifo, int sem, int totPartite){
    int currentPartita = 1;
    char winner[1];
    msg messaggio1, messaggio2;
    int fifo;

    if((fifo = open(pathFifo, O_WRONLY)) == -1){ //apertura fifo in un FILE* stream
        perror("open giudice");
        exit(1);
    }
    
    while(currentPartita <= totPartite){
        printf("[G] inizio partita n.%d\n", currentPartita);

        //chiedo ai player di giocare la loro mossa...
        memset(&messaggio1, 0, sizeof(msg)); //eof p1...
        messaggio1.eof = 0;
        messaggio1.type = 2;
        if((msgsnd(coda, &messaggio1, sizeof(msg)-sizeof(long), 0)) == -1){
            perror("msgsnd giudice eof m1");
            exit(1);
        }
        if((msgsnd(coda, &messaggio1, sizeof(msg)-sizeof(long), 0)) == -1){
            perror("msgsnd giudice eof m2");
            exit(1);
        }

        if((msgrcv(coda, &messaggio1, sizeof(msg)-sizeof(long), 1, 0)) == -1){
            perror("msgrcv giudice");
            exit(1);
        }
        if((msgrcv(coda, &messaggio2, sizeof(msg)-sizeof(long), 1, 0)) == -1){ //...prelevo i due messaggi dei player
            perror("msgrcv giudice");
            exit(1);
        }

        if((winner[0] = calcolaVincitore(messaggio1, messaggio2)) == 'e'){ //patta... verrà reiterata...
            printf("[G] partita n.%d patta e quindi da ripetere\n", currentPartita);
            continue;
        }
        else{
            printf("[G] partita n.%d vinta da P%c\n", currentPartita, winner[0]);
            write(fifo, winner, 1);
            SIGNAL(sem, S_TABELLONE); //sveglio tabellone...
            WAIT(sem, S_GIUDICE); //...e aspetto che finisca...
            currentPartita++;
        }
    }

    //le partite sono state giocate, in chiusura...
    winner[0] = 'e';
    write(fifo, winner, 1);
    SIGNAL(sem, S_TABELLONE);

    memset(&messaggio1, 0, sizeof(msg)); //eof p1...
    messaggio1.eof = 1;
    messaggio1.type = 2;
    if((msgsnd(coda, &messaggio1, sizeof(msg)-sizeof(long), 0)) == -1){
        perror("msgsnd giudice eof p1");
        exit(1);
    }
    if((msgsnd(coda, &messaggio1, sizeof(msg)-sizeof(long), 0)) == -1){
        perror("msgsnd giudice eof p2");
        exit(1);
    }

    printf("\t\t[G] terminazione...\n");
    close(fifo);
    exit(0);
}

char generaMossa(){
    int r = rand() % 3; //produce 1-2-3

    switch(r){
        case 0: return 's';
        case 1: return 'c';
        case 2: return 'f';
    }
}

void player(int coda, int sem, char id){
    srand(time(NULL) % getpid()); //generazione seme rand()
    msg messaggio;

    while(1){
        if((msgrcv(coda, &messaggio, sizeof(msg)-sizeof(long), 2, 0)) == -1){ //aspetto che il giudice mia dia il permesso di giocare la mia mossa...
            perror("msgrcv player");
            exit(1);
        }
        if(messaggio.eof)
            break;

        memset(&messaggio, 0, sizeof(msg));
        messaggio.mossa = generaMossa();
        messaggio.type = 1;
        messaggio.player = id;
        printf("[P%c] mossa '%c'\n", id, messaggio.mossa);

        if((msgsnd(coda, &messaggio, sizeof(msg)-sizeof(long), 0)) == -1){
            perror("msgsnd player");
            exit(1);
        }
    }

    printf("\t\t[P%c] terminazione...\n", id);
    exit(0);
}

int main(int argc, char *argv[]){
    int coda_d, fifo_d, sem_d;
    char *path = "/tmp/myfifo";

    if(argc != 2){
        printf("Uso: %s\n", argv[0]);
        exit(1);
    }

    if((coda_d = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione coda
        perror("msgget");
        exit(1);
    }
    if((fifo_d = mkfifo(path, 0600)) == -1){ //creazione fifo
        perror("mkfifo");
        exit(1);
    }
    if((sem_d = semget(IPC_PRIVATE, 2, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione vettore semafori
        perror("semget");
        exit(1);
    }

    //inizializzazione default semafori...
    if(semctl(sem_d, S_GIUDICE, SETVAL, 0) == -1){
        perror("semctl giudice");
        exit(1);
    }
    if(semctl(sem_d, S_TABELLONE, SETVAL, 0) == -1){
        perror("semctl tabellone");
        exit(1);
    }

    //creazione figli...
    if(fork() == 0)
        player(coda_d, sem_d, '1');
    if(fork() == 0)
        player(coda_d, sem_d, '2');
    if(fork() == 0)
        giudice(coda_d, path, sem_d, atoi(argv[1]));
    if(fork() == 0)
        tabellone(path, sem_d);
        
    wait(NULL);
    wait(NULL);
    wait(NULL);
    wait(NULL);

    //in chiusura...
    unlink(path); //cancellazione della fifo
    msgctl(coda_d, IPC_RMID, NULL);
    semctl(sem_d, 0, IPC_RMID, 0);

    printf("\t\t[PADRE] terminazione...\n");
    exit(0);
}