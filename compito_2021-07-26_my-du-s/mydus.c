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

#define MAX_PATH_LEN 2048

#define S_STATER 0
#define MUTEX 1
#define S_SCANNER_i 2

/***
 *      ||SHM_PATH :    (char *)
 *  | p+0 ==> usato da scanner[i] ==> path-di-ogni-file ==> race conditions => MUTEX
 * 
 *      ||SHM_COUNTER :    (int *)
 *  | p+0 ==> usato da scanner[i] ==> numero di scanner[i] da decrementare ==> race conditions => MUTEX
*/

typedef struct{
    long type;
    int eof;
    char pathFile[MAX_PATH_LEN];
    long nBlocks;
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
        lstat(entry->d_name, &statbuf); //raccolgo info sul file puntato

        if(S_ISDIR(statbuf.st_mode) && ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0))) //...se è "." o ".." vado avanti...
            continue;

        if(S_ISDIR(statbuf.st_mode)){ //...se è una directory avvio ricorsione su di essa...
            strcpy(previousDir, currentPath); //salvo la dir attuale per quando ritorno dalla ricorsione
            strcat(currentPath, entry->d_name);
            recursiveScan(currentPath, entry->d_name, sem, p);
            strcpy(currentPath, previousDir);
        }

        if(S_ISREG(statbuf.st_mode)){ //se è un file regolare...
            WAIT(sem, MUTEX); //chiedo permesso di scrivere nella shm
            strcpy(p, currentPath);
            strcat(p, entry->d_name); //...scrivo il path del file in shm
printf("___debug__writer[%s]___\n\t\tfile: %s\n", rootPath, p);
            SIGNAL(sem, S_STATER); //segnalo a stater che è presente un path da elaborare...
            WAIT(sem, S_SCANNER_i); //aspetto che stater finisca di elaborare
            SIGNAL(sem, MUTEX); //rilascio shared memory
        }
    }

    closedir(dir);
    return chdir(".."); //ritorno la directory di prima...
}

void scanner(int shm_p, int shm_c, int sem, char *rootPath){
    char *p_p, currentPath[MAX_PATH_LEN];
    int *p_c;

    if((p_p = (char*)shmat(shm_p, NULL, 0)) == (char*)-1){ //attach al segmento condiviso
        perror("shmat scanner");
        exit(1);
    }
    if((p_c = (int*)shmat(shm_c, NULL, 0)) == (int*)-1){ //attach alla shared memory
        perror("shmat stater");
        exit(1);
    }

    strcat(currentPath, rootPath);

    recursiveScan(currentPath, rootPath, sem, p_p);
    
    WAIT(sem, MUTEX);
    *p_c -= 1;
    if(*p_c == 0)
        SIGNAL(sem, S_STATER); //segnalo eof a
    SIGNAL(sem, MUTEX);

    shmdt(p_c);
    shmdt(p_p);
    printf("X\t\t[Wi] terminazione...\n");
    exit(0);
}

void stater(int shm_p, int shm_c, int sem, int coda, int nWi){
    struct stat statbuf;
    char *p_p;
    msg messaggio;
    int *p_c;

    if((p_p = (char*)shmat(shm_p, NULL, 0)) == (char*)-1){ //attach alla shared memory
        perror("shmat stater");
        exit(1);
    }
    if((p_c = (int*)shmat(shm_c, NULL, 0)) == (int*)-1){ //attach alla shared memory
        perror("shmat stater");
        exit(1);
    }

    messaggio.eof = 0;
    messaggio.type = 1;
    WAIT(sem, MUTEX);
    *p_c = nWi;
    SIGNAL(sem, MUTEX);

    for(int i = 0; i < nWi; i++)
        SIGNAL(sem, S_SCANNER_i); //ho settato l'eof=false, sveglio scanner[i]

    while(1){
        WAIT(sem, S_STATER); //aspetto che uno scanner mi confermi la presenza di dati da elaborare in shm...

        if(*p_c == 0)
            break;

        lstat(p_p, &statbuf); //ottengo info sul file...
        strcpy(messaggio.pathFile, p_p);
//printf("___debug_stater__nblock__ %ld\n", statbuf.st_blocks);
        messaggio.nBlocks = statbuf.st_blocks;

        if((msgsnd(coda, &messaggio, sizeof(msg)-sizeof(long), 0)) == -1){ //mando messaggio al padre...
            perror("msgsnd stater");
            exit(1);
        }

        SIGNAL(sem, S_SCANNER_i);
    }


    messaggio.eof = 1;
    if((msgsnd(coda, &messaggio, sizeof(msg)-sizeof(long), 0)) == -1){ //mando eof al padre...
        perror("msgsnd stater");
        exit(1);
    }

    //in chiusura...
    shmdt(p_p);
    shmdt(p_c);
    printf("X\t\t[STATER] terminazione...\n");
    exit(0);
}

int main(int argc, char *argv[]){
    int shmPath_d, shmWi_d, sem_d, coda_d;
    msg messaggio;
    long scannerBlocks[argc-1];

    memset(scannerBlocks, 0, sizeof(scannerBlocks));

    if(argc < 2){
        printf("Uso: %s <path-1> <path-2> ...\n", argv[0]);
        exit(1);
    }

    if((shmPath_d = shmget(IPC_PRIVATE, sizeof(char) * MAX_PATH_LEN+1, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione segmento condiviso
        perror("shmget path");
        exit(1);
    }
    if((shmWi_d = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione segmento condiviso
        perror("shmget w[i] counter");
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
    if(fork() == 0)
        stater(shmPath_d, shmWi_d, sem_d, coda_d, argc-1);
    for(int i = 1; i < argc; i++)
        if(fork() == 0){
            WAIT(sem_d, S_SCANNER_i); //aspetto che stater setti l'eof=false prima di tutti
            scanner(shmPath_d, shmWi_d, sem_d, argv[i]);
        }


    while(1){
        if((msgrcv(coda_d, &messaggio, sizeof(msg)-sizeof(long), 0, 0)) == -1){ //aspetto messaggio da stater...
            perror("msgrcv padre");
            exit(1);
        }

        if(messaggio.eof)
            break;

        for(int i = 0; i < argc-1; i++){ //salvo il numero di blocchi di uno specifico writer[i] path in un vettore
            if(strstr(messaggio.pathFile, argv[i+1]) != NULL){
                scannerBlocks[i] += messaggio.nBlocks;
                break;
            }
        }
    }

    for(int i = 0; i < argc; i++)
        wait(NULL);

    for(int i = 1; i < argc; i++)
        printf("%ld\t%s\n", scannerBlocks[i-1], argv[i]);

    //in chiusura...
    shmctl(shmPath_d, IPC_RMID, NULL);
    semctl(sem_d, 0, IPC_RMID, 0);

    printf("X\t\t[PADRE] terminazione...\n");
    exit(0); 
}