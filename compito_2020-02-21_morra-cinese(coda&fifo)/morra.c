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
 * 
 *  P.s. senza l'utilizzo di un semaforo per sincronizzare bene tabellone e giudice succede a volte
*   che il tabellone si desincronizza con il giudice
 */

#include <stdlib.h>
#include <stdio.h>
#include <wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <time.h>
#include <sys/stat.h>

#define FIFOPATH "/home/aremi/git/lab-os-unict/compito_2020-02-21_morra-cinese(coda&fifo)/myfifo"

typedef struct{
    long type;
    unsigned eof;
    char player;
    char mossa;
} codaMsg;

char generaMossa(){
    char pool[3] = {'C', 'F', 'S'};
    return pool[rand() % 3];
}

void player(char id, int coda_d){ //id, coda_d
    srand(time(NULL) % getpid());
    codaMsg msg;

    while(1){
        if((msgrcv(coda_d, &msg, sizeof(codaMsg)-sizeof(long), 2, 0)) == -1){ //aspetto che il giudice mi dia il permesso di giocare la mia mossa...
            perror("msgrcv player");
            exit(1);
        }

        if(msg.eof)
            break;

        memset(&msg, 0, sizeof(codaMsg));
        msg.type = 1;
        msg.player = id;
        msg.mossa = generaMossa();
        printf("[P%c]\t mossa '%c'\n", id, msg.mossa);

        if((msgsnd(coda_d, &msg, sizeof(codaMsg)-sizeof(long), 0)) == -1){
            perror("msgsnd player");
            exit(1);
        }
    }

    //in chiusura...
    printf("\t\t[P%c] terminazione...\n", id);
    exit(0);
}

char calcolaVincitore(codaMsg m1, codaMsg m2){
    switch(m1.mossa){
        case 'C':
            if(m2.mossa == 'C') return 'e';
            else if(m2.mossa == 'F') return '2';
            else return '1';
        case 'F':
            if(m2.mossa == 'C') return '1';
            else if(m2.mossa == 'F') return 'e';
            else return '2';
        case 'S':
            if(m2.mossa == 'C') return '2';
            else if(m2.mossa == 'F') return '1';
            else return 'e';
    }
}

void giudice(int coda_d, int nPartite){ //coda_d, fifo_d, numero-partite
    codaMsg msg1, msg2;
    int pCounter = 1;
    char winner;
    int fifo_d;
    FILE *f;

    if((fifo_d = open(FIFOPATH, O_WRONLY)) == -1){
        perror("open giudice fifo wOnly");
        exit(1);
    }

    while(pCounter <= nPartite){
        printf("[G]\t inizio partita n.%d\n", pCounter);

        memset(&msg1, 0, sizeof(codaMsg));
        msg1.type = 2;
        msg1.eof = 0;

        if((msgsnd(coda_d, &msg1, sizeof(codaMsg)-sizeof(long), 0)) == -1){ //attivo player.1
            perror("msgsnd 1 giudice");
            exit(1);
        }
        if((msgsnd(coda_d, &msg1, sizeof(codaMsg)-sizeof(long), 0)) == -1){ //attivo player.2
            perror("msgsnd 2 giudice");
            exit(1);
        }

        if((msgrcv(coda_d, &msg1, sizeof(codaMsg)-sizeof(long), 1, 0)) == -1){ //aspetto mossaP1
            perror("msgrcv 1 giudice");
            exit(1);
        }
        if((msgrcv(coda_d, &msg2, sizeof(codaMsg)-sizeof(long), 1, 0)) == -1){ //aspetto mossaP2
            perror("msgrcv 2 giudice");
            exit(1);
        }

        winner = calcolaVincitore(msg1, msg2);

        if(winner == 'e'){ //se è patta reitero...
            printf("[G]\t partita n.%d patta e quindi da ripetere\n", pCounter);
            continue;
        }

        printf("[G]\t partita n.%d vinta da P%c\n", pCounter, winner);

        if(write(fifo_d, &winner, 1) == -1){ //...altrimenti scrivo sulla fifo il vincitore
            perror("write fifo_d");
            exit(1);
        }

        pCounter++;
    }

    //in chiusura...
    memset(&msg1, 0, sizeof(codaMsg));
    msg1.eof = 1;
    msg1.type = 2;
    if((msgsnd(coda_d, &msg1, sizeof(codaMsg)-sizeof(long), 0)) == -1){ //eof primo player...
        perror("msgsnd 1 giudice");
        exit(1);
    }
    msg1.type = 2;
    if((msgsnd(coda_d, &msg1, sizeof(codaMsg)-sizeof(long), 0)) == -1){ //eof secondo player...
        perror("msgsnd 1 giudice");
        exit(1);
    }

    close(fifo_d); //dovrebbe manda eof a tabellone...
    printf("\t\t[G] terminazione...\n");
    exit(0);
}

void tabellone(){
    int fifo_d;
    int p1Win = 0, p2Win = 0;
    char winner;
    FILE *f;

    if((fifo_d = open(FIFOPATH, O_RDONLY)) == -1){ //apro la fifo in sola lettura
        perror("open tabellone fifo rOnly");
        exit(1);
    }
    if((f = fdopen(fifo_d, "r")) == NULL){ //se utilizzo la read classica si desincronizza sempre, con lo stream di meno
        perror("fdopen tabellone");
        exit(1);
    }

    while((winner = fgetc(f)) > 0){
        if(winner == '1')
            p1Win++;
        else
            p2Win++;

        printf("[T]\t classifca temporanea: P1=%d  P2=%d\n\n", p1Win, p2Win);
    }

    printf("[T]\t classifca finale: P1=%d  P2=%d\n", p1Win, p2Win);
    printf("[T]\t il vincitore del torneo è: P%c\n", p1Win > p2Win ? '1' : '2');

    //in chiusura...
    close(fifo_d);
    fclose(f);
    printf("\t\t[T] terminazione...\n");
    exit(0);
}

int main(int argc, char *argv[]){
    int coda_d;

    if(argc != 2){
        fprintf(stderr, "Uso: %s <numero-partite>\n", argv[0]);
        exit(1);
    }
    if((coda_d = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione coda
        perror("msgget");
        exit(1);
    }
    if(mkfifo(FIFOPATH, 0600) == -1){ //creazione fifo
        perror("mkfifo");
        exit(1);
    }

    //creazione figli
    if(fork() == 0)
        player('1', coda_d); //id, coda_d ==> player.1
    if(fork() == 0)
        player('2', coda_d); //id, coda_d ==> player.2
    if(fork() == 0)
        giudice(coda_d, atoi(argv[1])); //coda_d, numero-partite || FIFOPATH
    if(fork() == 0)
        tabellone(); //|| FIFOPATH

    wait(NULL);
    wait(NULL);
    wait(NULL);
    wait(NULL);

    //in chiusura...
    msgctl(coda_d, IPC_RMID, NULL);
    unlink(FIFOPATH);
    printf("\t\t[PADRE] terminazione...\n");
    exit(0);
}