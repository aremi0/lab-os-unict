/**
 * @file alpha.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-23
 * 
 * Prova di laboratorio di SO del 2021-02-10
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/mman.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>

#define S_AL 0
#define S_MZ 1
#define S_PADRE 2

int WAIT(int sem_des, int numSem){
    struct sembuf op[1] = {{numSem, -1, 0}};
    return semop(sem_des, op, 1);
}
int SIGNAL(int sem_des, int numSem){
    struct sembuf op[1] = {{numSem, +1, 0}};
    return semop(sem_des, op, 1);
}

void almz(char *shmC, long *shmS, int sem, unsigned SEM_N){
    while(1){
        WAIT(sem, SEM_N);

        if(shmC[0] == (char)-1) //se eof break...
            break;

printf("___debug___[%u] carattere letto[%c]\n", SEM_N, shmC[0]);
        shmS[(int)shmC[0] - (int)'a'] += 1;

        SIGNAL(sem, S_PADRE);
    }

    //eof...
    shmdt(shmC);
    shmdt(shmS);
    printf("\t\t[%u] terminazione...\n", SEM_N);
    exit(0);
}

int main(int argc, char *argv[]){
    int shmChar_d, shmStats_d, sem_d; //aggiungere mmap
    char *shmChar; //ptr
    long *shmStats; //ptr
    int input;
    struct stat statbuf;
    char *ptr; //puntatore RAM mmap()
    
    if(argc != 2){
        fprintf(stderr, "Uso: %s <input.txt>", argv[0]);
        exit(1);
    }
    if((input = open(argv[1], O_RDONLY)) == -1){ //apro il file input
        perror("fopen");
        exit(1);
    }
    if((stat(argv[1], &statbuf) == -1)){
        perror("stat");
        exit(1);
    }
    if((shmChar_d = shmget(IPC_PRIVATE, sizeof(char)*1, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione shm char
        perror("shmget char");
        exit(1);
    }
    if((shmStats_d = shmget(IPC_PRIVATE, sizeof(long)*26, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione shm stats
        perror("shmget stats");
        exit(1);
    }
    if((shmChar = (char*)shmat(shmChar_d, NULL, 0)) == (char*)-1){ //attach shmChar
        perror("shmat shmChar");
        exit(1);
    }
    if((shmStats = (long*)shmat(shmStats_d, NULL, 0)) == (long*)-1){ //attach shmStats
        perror("shmat shmStats");
        exit(1);
    }
    if((sem_d = semget(IPC_PRIVATE, 3, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione vettore dei semafori
        perror("semget");
        exit(1);
    }
    if((semctl(sem_d, S_AL, SETVAL, 0)) == -1){
        perror("semctl setval AL");
        exit(1);
    }
    if((semctl(sem_d, S_MZ, SETVAL, 0)) == -1){
        perror("semctl setval MZ");
        exit(1);
    }
    if((semctl(sem_d, S_PADRE, SETVAL, 0)) == -1){ //...inizializzazione default semafori
        perror("semctl setval S_padre");
        exit(1);
    }
    if((ptr = (char*)mmap(NULL, sizeof(char) * statbuf.st_size, PROT_READ, MAP_SHARED, input, 0)) == (char*)-1){ //mmap in RAM del file input
        perror("mmap");
        exit(1);
    }

    close(input); //chiudo il file, tanto è in RAM

    for(int i = 0; i < 26; i++) //azzero la shmStats...
        shmStats[i] = 0;

    //creazione dei processi figli
    if(fork() == 0)
        almz(shmChar, shmStats, sem_d, S_AL); //processo al
    if(fork() == 0)
        almz(shmChar, shmStats, sem_d, S_MZ); //processo mz

    //leggo la memoria e risveglio i figli per elaborare...
    for(int i = 0; i < statbuf.st_size; i++){
        shmChar[0] = (char)tolower(ptr[i]); //salvo nella shmC la tolower() del char che leggo...

        if(shmChar[0] >= 'a' && shmChar[0] <= 'l') //...e sveglio 'al' o 'mz' a seconda del caso
            SIGNAL(sem_d, S_AL);
        else if(shmChar[0] >= 'm' && shmChar[0] <= 'z')
            SIGNAL(sem_d, S_MZ);
        else //...ma se leggo un carattere speciale salto al prossimo char...
            continue;

        WAIT(sem_d, S_PADRE); //aspetto che il figlio elabori...
    }

    shmChar[0] = (char)-1; //mando eof...
    SIGNAL(sem_d, S_AL);
    SIGNAL(sem_d, S_MZ);

    wait(NULL);
    wait(NULL);

    //stampa...
    for(int i = 0; i < 26; i++)
        printf("[%c]:%ld  ", (char)((int)'A' + i), shmStats[i]);

    printf("\n");

    munmap(ptr, sizeof(char) * statbuf.st_size);
    semctl(sem_d, 0, IPC_RMID, 0);
    shmctl(shmChar_d, IPC_RMID, NULL);
    shmctl(shmStats_d, IPC_RMID, NULL);
    printf("\t\t[PADRE] terminazione...\n");
    exit(0);
}