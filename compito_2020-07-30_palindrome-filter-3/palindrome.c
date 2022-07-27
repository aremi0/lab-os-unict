/**
 * @file palindrome.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-26
 * 
 * Prova di laboratorio di SO del 2020-07-30
 * 
 * P.s. forse si può diminuire il numero di semafori a 2? bhu, non penso perchè
 *      perderei la sincronia fra i 3 processi...
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <ctype.h>

#define MAX_LEN 64

#define S_READER 0
#define S_WRITER 1
#define S_PADRE 2

typedef struct{
    unsigned eof;
    char parola[MAX_LEN];
} shmMsg;

int WAIT(int sem_des, int numSem){
    struct sembuf op[1] = {{numSem, -1, 0}};
    return semop(sem_des, op, 1);
}
int SIGNAL(int sem_des, int numSem){
    struct sembuf op[1] = {{numSem, +1, 0}};
    return semop(sem_des, op, 1);
}

void reader(shmMsg* ptr, int sem, char *inputPath){ //ptr_shm, sem_d, argv[1]
    FILE *input;

    if(inputPath && ((input = fopen(inputPath, "r")) == NULL)){ //apro file input in uno stream
        perror("fopen reader");
        exit(1);
    }        

    ptr->eof = 0;

    if(inputPath){
        while(fgets(ptr->parola, MAX_LEN, input)){ //mentre leggo righe dal file...
        SIGNAL(sem, S_PADRE);
        WAIT(sem, S_READER);
        }
    }
    else{
        while(1){ //mentre leggo righe dalla tastiera...
        printf("[R] digita una parola; 'exit' per uscire\n");
        fgets(ptr->parola, MAX_LEN, stdin);

        if(strcmp(ptr->parola, "exit\n") == 0)
            break;

            SIGNAL(sem, S_PADRE);
            WAIT(sem, S_READER);
        }
    }

    ptr->eof = 1;
    SIGNAL(sem, S_PADRE); //mando eof
    SIGNAL(sem, S_WRITER); //mando eof

    //in chiusura...
    if(inputPath)
        fclose(input);
    shmdt(ptr);
    printf("\t\t[R] terminazione...\n");
    exit(0);
}

void writer(shmMsg *ptr, int sem, char *outputPath){ //se è specificato un file di output scrivo solo lì, altrimenti stampo sul terminale...
    FILE *output;

    if(outputPath && (output = fopen(outputPath, "w")) == NULL){ //apro file output in uno stream
        perror("fopen reader");
        exit(1);
    }

    while(1){
        WAIT(sem, S_WRITER); //aspetto che il padre mi svegli per scrivere la parola in output

        if(ptr->eof)
            break;

        if(outputPath)
            fputs(ptr->parola, output);
        else
            printf("[W] palindroma: %s", ptr->parola);

        SIGNAL(sem, S_READER);
    }

    if(outputPath){
        printf("[W] file scritto!\n");
        fclose(output);
    }

    //in chiusura...
    shmdt(ptr);
    printf("\t\t[W] terminazione...\n");
    exit(0);
}

unsigned isPalindrome(char *parola){
    for(int i = 0, j = strlen(parola)-2; i <= j; i++, j--){
        if(i == j) return 1; //se la stringa è dispari e palindroma...
        else if(tolower(parola[i]) != tolower(parola[j])) return 0;
    }

    return 1;
}

int main(int argc, char *argv[]){
    int shm_d, sem_d;
    shmMsg *ptr;

    if(argc > 3){
        fprintf(stderr, "Uso: %s [input-file] [output-file]", argv[0]);
        exit(1);
    }
    if((shm_d = shmget(IPC_PRIVATE, sizeof(shmMsg), IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione segmento condiviso
        perror("shmget");
        exit(1);
    }
    if((ptr = (shmMsg*)shmat(shm_d, NULL, 0)) == (shmMsg*)-1){ //attach shm
        perror("shmat");
        exit(1);
    }
    if((sem_d = semget(IPC_PRIVATE, 3, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione vettore semafori
        perror("semget");
        exit(1);
    }
    if((semctl(sem_d, S_READER, SETVAL, 0)) == -1){
        perror("semctl setval reader");
        exit(1);
    }
    if((semctl(sem_d, S_WRITER, SETVAL, 0)) == -1){
        perror("semctl setval writer");
        exit(1);
    }
    if((semctl(sem_d, S_PADRE, SETVAL, 0)) == -1){ //...inizializzazone default semafori
        perror("semctl setval padre");
        exit(1);
    }

    //creazione figli
    if(fork() == 0){
        if(argc > 2)
            writer(ptr, sem_d, argv[2]); //ptr_shm, sem_d, argv[2]
        else
            writer(ptr, sem_d, NULL);
    }
    if(fork() == 0){
        if(argc > 1)
            reader(ptr, sem_d, argv[1]); //ptr_shm, sem_d, argv[1]
        else
            reader(ptr, sem_d, NULL);
    }

    while(1){
        WAIT(sem_d, S_PADRE); //aspetto che il reader mi segnali la presenza di dati sulla shm da elaborare...

        if(ptr->eof)
            break;

        if(isPalindrome(ptr->parola))
            SIGNAL(sem_d, S_WRITER); //se è palindroma sveglio il writer...
        else
            SIGNAL(sem_d, S_READER);

    }

    //in chiusura...
    shmdt(ptr);
    shmctl(sem_d, IPC_RMID, NULL);
    semctl(sem_d, 0, IPC_RMID, 0);
    printf("\t\t[PADRE] terminazione...\n");
    exit(0);
}