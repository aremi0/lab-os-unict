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

#define MAX_PATH_LEN 4096

#define S_STATER 0      //sync
#define MUTEX 1         //mutex-sync

typedef struct{
    unsigned eof;
    unsigned id;
    char pathFile[MAX_PATH_LEN];
}shm_msg;

typedef struct{
    long type;
    unsigned eof;
    unsigned id;
    unsigned long nBlocks;
} msg;

int WAIT(int sem_des, int semNum){
    struct sembuf op[1] = {{semNum, -1, 0}};
    return semop(sem_des, op, 1);
}

int SIGNAL(int sem_des, int semNum){
    struct sembuf op[1] = {{semNum, +1, 0}};
    return semop(sem_des, op, 1);
}

void scanner(char id, int shm, int sem, char *path, char casoBase){
    DIR *dir;
    struct dirent *entry;
    shm_msg *p;

    if((p = (shm_msg*)shmat(shm, NULL, 0)) == (shm_msg*)-1){ //attach al segmento condiviso path
        perror("shmat scanner");
        exit(1);
    }
    if((dir = opendir(path)) == NULL){ //apro la directory del path
        perror("opendir scanner recursion");
        exit(1);
    }

    while((entry = readdir(dir))){ //mentre punto un file della directory...
        if((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) //se punto la dir "." o ".." vado avanti
            continue;
        else if(entry->d_type == DT_DIR){ //se è una qualsiasi altra dir avvio ricorsione su di essa...
            char tmp[MAX_PATH_LEN];
            sprintf(tmp, "%s/%s", path, entry->d_name);
            scanner(id, shm, sem, tmp, 0);
        }
        else if(entry->d_type == DT_REG){ //se è un file regolare...
            WAIT(sem, MUTEX); //chiedo permesso di scrivere nella shm
            sprintf(p->pathFile, "%s/%s", path, entry->d_name);
            p->eof = 0;
            p->id = id; //...scrivo il path del file in shm
            SIGNAL(sem, S_STATER); //segnalo a stater che è presente un path da elaborare... rilascerà lui la shm
        }
    }

    closedir(dir);

    if(casoBase){
        WAIT(sem, MUTEX);
        p->eof = 1;
        SIGNAL(sem, S_STATER);
        shmdt(p);
        printf("\t\t[SCANNER[%d]] terminazione...\n", id);
        exit(0);
    }
}

void stater(int shm, int sem, int coda, int numScanner){
    struct stat statbuf;
    shm_msg *p;
    msg messaggio;
    unsigned eofCounter = 0;

    messaggio.eof = 0;
    messaggio.type = 1;

    if((p = (shm_msg*)shmat(shm, NULL, 0)) == (shm_msg*)-1){ //attach al segmento condiviso path
        perror("shmat stater");
        exit(1);
    }

    while(1){
        WAIT(sem, S_STATER); //aspetto che uno scanner mi confermi la presenza di un path da elaborare in shm...

        if(p->eof){ //se è true mi hanno svegliato per segnalarmi una exit() e non un nuovo path
            eofCounter++;

            if(eofCounter == numScanner)
                break;
            else{
                SIGNAL(sem, MUTEX); //rilascio shm e continuo al prossimo ciclo di attesa...
                continue;
            }
        }
    

        lstat(p->pathFile, &statbuf); //ottengo info sul file...
        messaggio.id = p->id;
        messaggio.nBlocks = statbuf.st_blocks;
        SIGNAL(sem, MUTEX); //posso rilasciare shm

        if((msgsnd(coda, &messaggio, sizeof(msg)-sizeof(long), 0)) == -1){ //mando messaggio al padre...
            perror("msgsnd stater");
            exit(1);
        }
    }

    messaggio.eof = 1;

    if((msgsnd(coda, &messaggio, sizeof(msg)-sizeof(long), 0)) == -1){ //mando eof al padre...
        perror("msgsnd stater");
        exit(1);
    }

    //in chiusura...
    shmdt(p);
    printf("\t\t[STATER] terminazione...\n");
    exit(0);
}

int main(int argc, char *argv[]){
    int shm_d, sem_d, coda_d;
    msg messaggio;
    unsigned long scannerBlocks[argc-1];

    for(int i = 0; i < argc-1; i++)
        scannerBlocks[i] = 0;

    //memset(&scannerBlocks, 0, sizeof(scannerBlocks)); //azzero vettore dei risultati finali scannerBlocks

    if(argc < 2){
        printf("Uso: %s <path-1> <path-2> ...\n", argv[0]);
        exit(1);
    }

    if((shm_d = shmget(IPC_PRIVATE, sizeof(shm_msg), IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione segmento condiviso
        perror("shmget w[i] counter");
        exit(1);
    }
    if((coda_d = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione coda
        perror("shmat");
        exit(1);
    }
    if((sem_d = semget(IPC_PRIVATE, 2, IPC_CREAT | IPC_EXCL | 0600)) == -1){ //creazione vettore semafori
        perror("semget");
        exit(1);
    }
    if((semctl(sem_d, S_STATER, SETVAL, 0)) == 1){
        perror("semctl setval bho");
        exit(1);
    }
    if((semctl(sem_d, MUTEX, SETVAL, 1)) == 1){ //...inizializzazione default semafori
        perror("semctl setval bho");
        exit(1);
    }

    //creazione figli...
    if(fork() == 0)
        stater(shm_d, sem_d, coda_d, argc-1);
    for(int i = 1; i < argc; i++)
        if(fork() == 0)
            scanner(i-1, shm_d, sem_d, argv[i], 1);


    while(1){
        if((msgrcv(coda_d, &messaggio, sizeof(msg)-sizeof(long), 0, 0)) == -1){ //aspetto messaggio da stater...
            perror("msgrcv padre");
            exit(1);
        }

        if(messaggio.eof)
            break;

        scannerBlocks[messaggio.id] += messaggio.nBlocks;

        // for(int i = 0; i < argc-1; i++){ //salvo il numero di blocchi di uno specifico writer[i] path in un vettore
        //     if(strstr(messaggio.pathFile, argv[i+1]) != NULL){ //bello sto metodo, ma lo sai cosa comporta vero?
        //         scannerBlocks[i] += messaggio.nBlocks / 2; //sto diviso due non lo so perchè...
        //         break;
        //     }
        // }
    }

    for(int i = 0; i < argc; i++)
        wait(NULL);

    for(int i = 0; i < argc-1; i++)
        printf("%ld\t%s\n", scannerBlocks[i], argv[i+1]);

    //in chiusura...
    shmctl(shm_d, IPC_RMID, NULL);
    semctl(sem_d, 0, IPC_RMID, 0);

    printf("\t\t[PADRE] terminazione...\n");
    exit(0); 
}