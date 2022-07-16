/**
 * @file search.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-16
 * 
 * Prova di laboratorio di SO del 2021/09/28
 */

#define _GNU_SOURCE //per strcasestr()

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#define DIMBUF 2048

#define S_R 0
#define S_Wi 1

/***
 *  ||Inter-Process-Comunication TABLE :
 *  |--- [P] crea pipe_d e SHM.
 *  |--- [READER] manda ai [WRITER[i]] un shm+1[DIMBUF] per ogni riga del file.
 *  |--- [READER] manda a [OUTPUT] sulla 'pipe' solo riga matchata.
*/

/***
 *  ||SHM : ==> (char*)
 *  |--- p[0] =>  char match   *(shm+0)  ====>  in questa locazione, inizialmente posta a argc-2, i processi WRITER[i]
 *  |--- p+1  =>  DIMBUF char*                  faranno un decremento -1 (con opportuno cast a char) se trovano un match.
 *        |                                     Il processo READER dovra' controllare che 'p[0] == (char) 0'
 *        -> qui ci salvo la riga.|             Ovvero che tutti i writer hanno matchato...
*/

int WAIT(int sem_d, int numSem){
    struct sembuf op[1] = {{numSem, -1, 0}};
    return semop(sem_d, op, 1);
}
int SIGNAL(int sem_d, int numSem){
    struct sembuf op[1] = {{numSem, +1, 0}};
    return semop(sem_d, op, 1);
}

void output(int pipe_d){
    FILE *_pipe;
    char buffer[DIMBUF];

    if((_pipe = fdopen(pipe_d, "r")) == NULL){ //apro canale lettura pipe in uno stream in lettura...
        perror("fopen output");
        exit(1);
    }

    while(fgets(buffer, DIMBUF, _pipe)){ //mentre leggo dalla pipe (non-null)

        if(buffer[0] == (char) -1) //se legge eof...
            break;

        printf("%s\n\n", buffer); //...stampo in output...
    }
    
    //in chiusura...
    close(pipe_d);
    printf("\t\t[O] terminazione...\n");
    exit(0);
}

void writer(int sem, int shm, char *myIdParola){
    char *p, *match, *riga; //puntatori diretti alla shm

    if((p = (char*)shmat(shm, NULL, 0)) == (char*)-1){ //attach alla shared memory
        perror("shmat writer");
        exit(1);
    }

    //setto i puntatori diretti alla shm
    match = p; //p[0]
    riga = p+1; //da p[1] in poi...

    while(1){
        WAIT(sem, S_Wi); //aspetto che reader mi dia il permesso di elaborare la riga...

        if(*match == (char) -1)
            break;

        if((strcasestr(riga, myIdParola)) != NULL) //ha trovato un match
            *match = (char)((int)*match - 1); //...decremento match...

        SIGNAL(sem, S_R); //riga elaborata... sveglio il reader
    }

    //in chiusura...
    shmdt(p);
    printf("\t\t[W] terminazione...\n");
    exit(0);
}

void reader(int sem, int shm, int pipe_d, int numWi, char *pathFile){
    char *p, *match, *riga; //puntatori diretti alla shm
    FILE *input;

    if((p = (char*)shmat(shm, NULL, 0)) == (char*)-1){ //attach alla shared memory
        perror("shmat reader");
        exit(1);
    }

    //setto i puntatori diretti alla shm
    match = p; //p[0]
    riga = p+1; //da p[1] in poi...
    *match = (char)numWi; //setto il match della shm uguale al numero totale di W[i] in esecuzione (loro dovranno quindi decrementare questo valore).

    if((input = fopen(pathFile, "r")) == NULL){ //apertura file di input da cui leggere le righe
        perror("fopen reader");
        exit(1);
    }

    while(fgets(riga, DIMBUF, input)){ //per ogni riga che leggo la azzicco dentro la shm+1
        for(int i = 0; i < numWi; i++)
            SIGNAL(sem, S_Wi); //sveglio i W[i] writer...

        for(int i = 0; i < numWi; i++)
            WAIT(sem, S_R); //aspetto che tutti i W[i] finiscano di elaborare la riga...

        if((int)*match == 0) //tutti i writer hanno matchato...
            write(pipe_d, riga, strlen(riga)); //...scrivo sulla pipe
        
        *match = (char)numWi;
    }

    //in chiusura, mando eof, shmdetach e exit()...
    *match = (char) -1; //eof ai processi w[i]
    for(int i = 0; i < numWi; i++)
        SIGNAL(sem, S_Wi); //sveglio i W[i] writer...
    *riga = (char) -1;
    write(pipe_d, riga, strlen(riga));

    close(pipe_d);
    fclose(input);
    shmdt(p);
    printf("\t\t[R] terminazione...\n");
    exit(0);
}

int main(int argc, char *argv[]){
    int pipe_d[2], sem_d, shm_d;

    if(argc < 3){
        printf("Uso: %s\n", argv[0]);
        exit(1);
    }

    if(pipe(pipe_d) == -1){ //creazione pipe
        perror("pipe");
        exit(1);
    }
    //creazione shm. attach nei processi specifici...
    if((shm_d = shmget(IPC_PRIVATE, sizeof(char) * (DIMBUF + 1), IPC_CREAT | IPC_EXCL | 0600)) == -1){
        perror("shmget");
        exit(1);
    }
    if((sem_d = semget(IPC_PRIVATE, 2, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione vettore semafori
        perror("semget");
        exit(1);
    }
    if(semctl(sem_d, S_R, SETVAL, 0) == -1){
        perror("semctl setval reader");
        exit(1);
    }
    if(semctl(sem_d, S_Wi, SETVAL, 0) == -1){ //setto lo stato default dei semaforo...
        perror("semctl setval Wi");
        exit(1);
    }

    //creazione figli...
    if(fork() == 0){
        //close(pipe_d[0]); //chiudo canale lettura pipe
        reader(sem_d, shm_d, pipe_d[1], argc-2, argv[1]);
    }
    if(fork() == 0){
        close(pipe_d[1]); //chiudo canale scrittra pipe
        output(pipe_d[0]);
    }
    for(int i = 2; i < argc; i++)
        if(fork() == 0)
            writer(sem_d, shm_d, argv[i]);

    for(int i = 0; i < argc; i++)
        wait(NULL);

    //in chiusura...
    close(pipe_d[0]);
    close(pipe_d[1]);
    shmctl(shm_d, IPC_RMID, NULL);
    semctl(sem_d, 0, IPC_RMID, 0);
    printf("\t\t[PADRE] terminazione...\n");
    exit(0);
}