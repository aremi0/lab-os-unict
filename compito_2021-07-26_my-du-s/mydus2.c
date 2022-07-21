/**
 * @file mydus2.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-21
 * 
 * Prova di laboratorio di so del 2021-07-26
 *      ||uguale all'altro, semplicemente riscritto da capo il giorno dopo
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>

#define S_STATER 0
#define S_MUTEX 1

typedef struct{
    long type;
    unsigned eof;
    unsigned id;
    long nBlocks;
} msgCoda;

typedef struct{
    unsigned eof;
    unsigned id;
    char path[PATH_MAX];
} msgShm;

/***
 *  scanner[i] => stater        ==> shm
 *  stater => padre             ==> coda
*/

int WAIT(int sem_des, int numSem){
    struct sembuf op[1] = {{numSem, -1, 0}};
    return semop(sem_des, op, 1);
}
int SIGNAL(int sem_des, int numSem){
    struct sembuf op[1] = {{numSem, +1, 0}};
    return semop(sem_des, op, 1);
}

void scanner(unsigned id, int shm, int sem, char *path, unsigned casoBase){ //id, shm, sem, path, casoBase
    msgShm *ptr;
    DIR *dir;
    struct dirent *entry;
    
    if((ptr = (msgShm*)shmat(shm, NULL, 0)) == (msgShm*)-1){
        perror("shmat scanner");
        exit(1);
    }
    if((dir = opendir(path)) == NULL){
        perror("opendir scanner");
        exit(1);
    }

    while(entry = readdir(dir)){
        if((strcmp(".", entry->d_name) == 0) || (strcmp("..", entry->d_name) == 0))
            continue;
        else if(entry->d_type == DT_REG){
            WAIT(sem, S_MUTEX);
            ptr->eof = 0;
            ptr->id = id;
            sprintf(ptr->path, "%s/%s", path, entry->d_name);
            SIGNAL(sem, S_STATER); //passo la mutua esclusione a stater, rilascerà lui la shm...
        }
        else if(entry->d_type == DT_DIR){
            char tmp[PATH_MAX];
            sprintf(tmp, "%s/%s", path, entry->d_name);
            scanner(id, shm, sem, tmp, 0); //entro nella sub-dir quindi non è il caso-base (il caso-base corrisponde alla directory da cui parte la ricorsione)
        }
    }

    closedir(dir);

    if(casoBase){
        WAIT(sem, S_MUTEX);
        ptr->eof = 1;
        SIGNAL(sem, S_STATER); //passo la mutua esclusione a stater, rilascerà lui la shm...
        shmdt(ptr);
        printf("[SCANNER[%u]] terminazione...\n", id);
        exit(0);
    }
}

void stater(int shm, int coda, int sem, unsigned numScanner){ //shm, coda, sem, numero di scanner
    unsigned eofCounter = 0;
    msgCoda msg;
    msgShm *ptr;
    struct stat statbuf;


    msg.eof = 0;
    msg.type = 1;

    if((ptr = (msgShm*)shmat(shm, NULL, 0)) == (msgShm*)-1){
        perror("shmat stater");
        exit(1);
    }

    while(1){
        WAIT(sem, S_STATER);

        if(ptr->eof){
            eofCounter++;

            if(eofCounter == numScanner)
                break;
            else{
                SIGNAL(sem, S_MUTEX); //rilascio shm...
                continue;
            }
        }

        lstat(ptr->path, &statbuf);
        msg.id = ptr->id;
        msg.nBlocks = statbuf.st_blocks;
        SIGNAL(sem, S_MUTEX); //rilascio shm...

        if((msgsnd(coda, &msg, sizeof(msgCoda)-sizeof(long), 0)) == -1){
            perror("msgsnd stater");
            exit(1);
        }
    }

    //in chiusura...
    msg.eof = 1;
    if((msgsnd(coda, &msg, sizeof(msgCoda)-sizeof(long), 0)) == -1){
        perror("msgsnd stater");
        exit(1);
    }

    shmdt(ptr);
    printf("[STATER] terminazione...\n");
    exit(0);
}

int main(int argc, char *argv[]){
    int shm_d, coda_d, sem_d;
    msgCoda msg;
    long totBlocks[argc-1];

    for(int i = 0; i < argc-1; i++)
        totBlocks[i] = 0;

    if(argc < 2){
        fprintf(stderr, "Uso: %s <path-1> <path-2> ...\n", argv[0]);
        exit(1);
    }

    if((coda_d = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){
        perror("msgget");
        exit(1);
    }
    if((shm_d = shmget(IPC_PRIVATE, sizeof(msgShm), IPC_CREAT | IPC_EXCL | 0600)) == -1){
        perror("shmget");
        exit(1);
    }
    if((sem_d = semget(IPC_PRIVATE, 2, IPC_CREAT | IPC_EXCL | 0600)) == -1){
        perror("semget");
        exit(1);
    }
    if((semctl(sem_d, S_STATER, SETVAL, 0)) == -1){
        perror("semctl setval stater");
        exit(1);
    }
    if((semctl(sem_d, S_MUTEX, SETVAL, 1)) == -1){
        perror("semctl setval mutex");
        exit(1);
    }

    if(fork() == 0)
        stater(shm_d, coda_d, sem_d, argc-1); //shm, coda, sem, numero di scanner
    for(int i = 1; i < argc; i++)
        if(fork() == 0)
            scanner(i-1, shm_d, sem_d, argv[i], 1); //id, shm, sem, path, casoBase

    while(1){
        if((msgrcv(coda_d, &msg, sizeof(msgCoda)-sizeof(long), 0, 0)) == -1){
            perror("msgrcv padre");
            exit(1);
        }

        if(msg.eof)
            break;

        totBlocks[msg.id] += msg.nBlocks;
    }

    for(int i = 0; i < argc-1; i++)
        wait(NULL);

    for(int i = 0; i < argc-1; i++)
        printf("%ld\t%s\n", totBlocks[i], argv[i+1]);

    //in chiusura...
    shmctl(shm_d, IPC_RMID, NULL);
    semctl(sem_d, 0, IPC_RMID, 0);
    msgctl(coda_d, IPC_RMID, NULL);

    printf("[PADRE] terminazione...\n");
    exit(0);
}