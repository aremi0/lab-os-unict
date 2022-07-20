/**
 * @file mydus.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-19
 * 
 * Prova di laboratorio di SO del 2021-07-26
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <wait.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <dirent.h>

#define MAX_PATH_LEN 1024

#define S_STATER 0
#define MUTEX 1
#define S_SCANNER_i 2

/***
 *      ||SHM :    (char **)
 *  | p+0 = eof     ----> eof p[0] ==> '1'
 *  | p+1 ==> usato da scanner[i] ==> path-file ==> race conditions => MUTEX
*/

typedef struct{
    long type;
    int eof;
    char pathFile[MAX_PATH_LEN];
} msg;

int WAIT(int sem_des, int semNum){
    struct sembuf op[1] = {{semNum, -1, 0}};
    return semop(sem_des, op, 1);
}

int SIGNAL(int sem_des, int semNum){
    struct sembuf op[1] = {{semNum, +1, 0}};
    return semop(sem_des, op, 1);
}

int recursiveScan(char* currentPath, char* rootPath, int sem, char *p){
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    char previousDir[MAX_PATH_LEN];

    if((dir = opendir(rootPath)) == NULL){ //apro la directory del path
        perror("opendir scanner recursion");
        exit(1);
    }
    if((chdir(rootPath)) == -1){ //cambio la current-working-directory alla directory da scansionare...
        perror("chdir scanner recursion");
        exit(1);
    }
    strcat(currentPath, "/"); //predispongo currentPath alla concatenazione...
    
    while((entry = readdir(dir))){ //mentre punto un file della directory...
        stat(entry->d_name, &statbuf); //raccolgo info sul file puntato

        if(S_ISDIR(statbuf.st_mode) && ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0))) //...se è "." o ".." vado avanti...
            continue;

        if(S_ISDIR(statbuf.st_mode)){ //...se è una directory avvio ricorsione su di essa...
            strcpy(previousDir, currentPath); //salvo la dir attuale per quando ritorno dalla ricorsione
            strcat(currentPath, entry->d_name);
            recursiveScan(currentPath, entry->d_name, sem, p);
            strcpy(currentPath, previousDir);
        }

        if(S_ISREG(statbuf.st_mode)){ //se è un file regolare...
            //WAIT(sem, MUTEX); //chiedo permesso di scrivere nella shm
            strcpy(p+1, currentPath);
            strcat(p+1, entry->d_name); //...scrivo il path del file in shm
printf("___debug__writer[%s]___\n\t\tfile<%s>___\n", rootPath, p+1);
            //SIGNAL(sem, S_STATER); //segnalo a stater che è presente un path da elaborare...
            //WAIT(sem, S_SCANNER_i); //aspetto che stater finisca di elaborare
            //SIGNAL(sem, MUTEX); //rilascio shared memory
        }
    }

    return chdir(".."); //ritorno la directory di prima...
}

void scanner(int shm, int sem, char *rootPath){
    char *p, currentPath[MAX_PATH_LEN];

    if((p = (char*)shmat(shm, NULL, 0)) == (char*)-1){ //attach al segmento condiviso
        perror("shmat scanner");
        exit(1);
    }

    strcat(currentPath, rootPath);

    recursiveScan(currentPath, rootPath, sem, p);
    

}

int main(int argc, char *argv[]){
    int shm_d, sem_d, coda_d;

    if(argc < 2){
        printf("Uso: %s <path-1> <path-2> ...\n", argv[0]);
        exit(1);
    }

    if((shm_d = shmget(IPC_PRIVATE, sizeof(char) * MAX_PATH_LEN+1, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione segmento condiviso
        perror("shmget");
        exit(1);
    }
    if((coda_d = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione coda
        perror("shmat");
        exit(1);
    }
    if((sem_d = semget(IPC_PRIVATE, 3, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione vettore semafori
        perror("semget");
        exit(1);
    }
    if((semctl(sem_d, S_STATER, SETVAL, 0)) == 1){
        perror("semctl setval bho");
        exit(1);
    }
    if((semctl(sem_d, S_SCANNER_i, SETVAL, 0)) == 1){
        perror("semctl setval bho");
        exit(1);
    }
    if((semctl(sem_d, MUTEX, SETVAL, 1)) == 1){ //...inizializzazione default semafori
        perror("semctl setval bho1");
        exit(1);
    }

    //creazione figli...
    /*if(fork() == 0)
        stater(shm_d, sem_d);*/
    for(int i = 1; i < argc; i++)
        if(fork() == 0)
            scanner(shm_d, sem_d, argv[i]);






    for(int i = 0; i < argc; i++)
        wait(NULL);

/*    //in chiusura...
    shmctl(shm_d, IPC_RMID, NULL);
    semctl(sem_d, 0, IPC_RMID, 0);

    printf("\t\t[PADRE] terminazione...\n");
    exit(0); */
}