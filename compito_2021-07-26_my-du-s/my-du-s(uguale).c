#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

/** Svolto durante il tutorato 19-20. Da rivedere **/

#define MAX_PATH_LEN 512
#define QUIT_MSG_STR "quit"

// Esercizio: Si possono utilizzare meno semafori?
#define TERMINAL_READ_SEMID 0
#define TERMINAL_WRITE_SEMID 1
#define STATER_READ_SEMID 2
#define STATER_WRITE_SEMID 3
#define SCANNER_READ_SEMID 4
#define SCANNER_WRITE_SEMID 5

typedef struct message {
    uint scannerId;
    char path[MAX_PATH_LEN];
    size_t size;
} message_t;

/** Helpers semafori **/
int WAIT(int sem_des, int num_semaforo) {
    struct sembuf operazioni[1] = {{num_semaforo,-1,0}};
    return semop(sem_des, operazioni, 1);
}

int SIGNAL(int sem_des, int num_semaforo) {
    struct sembuf operazioni[1] = {{num_semaforo,+1,0}};
    return semop(sem_des, operazioni, 1);
}

/** Funzioni Scanner **/
int scanDirectory(uint scannerId, char* path, message_t* msgZone, int semaphoresDescriptor) {
    size_t pathLen = strlen(path);
    if(path[pathLen-1] != '/') {
       path[pathLen] = '/';
       path[pathLen+1] = 0;
    }

    DIR* dir = opendir(path);

    if(dir == NULL) {
        fprintf(stderr, "opendir %s: %s\n", path, strerror(errno));
        return -2;
    }

    struct dirent* dirEntity;
    struct stat statBuffer;
    char fileFullPath[MAX_PATH_LEN];

    while((dirEntity = readdir(dir)) != NULL) {
        char* fileName = dirEntity->d_name;
        if(strcmp(fileName, ".") == 0 || strcmp(fileName, "..") == 0) {
            continue;
        }

        sprintf(fileFullPath, "%s%s", path, fileName);

        if(lstat(fileFullPath, &statBuffer) != 0) {
            fprintf(stderr, "stat of %s: %s\n", fileFullPath, strerror(errno));
            continue;
        } 

        if(S_ISLNK(statBuffer.st_mode)) {
            //printf("DEBUG: %s è un link simbolico\n", fileFullPath);
            continue;
        }

        if(S_ISREG(statBuffer.st_mode)) {
            //printf("DEBUG: Invio del percorso %s\n", fileFullPath);

            WAIT(semaphoresDescriptor, SCANNER_WRITE_SEMID);
            msgZone->scannerId = scannerId;
            strcpy(msgZone->path, fileFullPath);
            SIGNAL(semaphoresDescriptor, STATER_READ_SEMID);

            continue;
        } 

        if(S_ISDIR(statBuffer.st_mode)) {
            //printf("DEBUG: Analisi ricorsiva della cartella %s\n", fileFullPath);
            scanDirectory(scannerId, fileFullPath, msgZone, semaphoresDescriptor);
            continue;
        }
    }

    if(closedir(dir) != 0) {
        fprintf(stderr, "closedir %s: %s\n", fileFullPath, strerror(errno));
        return -2;
    }

    return 0;
}

int scanner(uint id, char* rootPath, message_t* msgZone, int semaphoresDescriptor) {
    //printf("[Scanner (%s)] Avvio\n", rootPath);

    // Scansione della directory
    int retcode = scanDirectory(id, rootPath, msgZone, semaphoresDescriptor);

    WAIT(semaphoresDescriptor, SCANNER_WRITE_SEMID);

    //printf("[Scanner (%s)] Chiusura...\n", rootPath);
    strcpy(msgZone->path, QUIT_MSG_STR);

    SIGNAL(semaphoresDescriptor, STATER_READ_SEMID);

    return retcode;
}

/** Funzioni Stater **/
int stater(message_t* msgZone, int semaphoresDescriptor, size_t scannersCount) {
    //printf("[Stater] Avvio\n");
    
    size_t currentScanners = scannersCount;

    struct stat statBuffer;

    while(currentScanners > 0) {
        SIGNAL(semaphoresDescriptor, SCANNER_WRITE_SEMID);

        WAIT(semaphoresDescriptor, STATER_READ_SEMID);
        //printf("[Stater] Sto leggendo la memoria... (%s)\n", msgZone->path);

        if(strcmp(msgZone->path, QUIT_MSG_STR) == 0) {
            currentScanners -= 1;
        } else {
            // Recupera la dimensione del file
            if(stat(msgZone->path, &statBuffer) != 0) {
                perror("scanner fstat");
                strcmp(msgZone->path, QUIT_MSG_STR);
            } else { 
                size_t fileDimension = statBuffer.st_blocks;

                //printf("[Stater] Scrivo la dimensione... (%lu)\n", fileDimension);
                msgZone->size = fileDimension;
            }

            SIGNAL(semaphoresDescriptor, TERMINAL_READ_SEMID);

            // Aspetta per la conferma del terminale 
            WAIT(semaphoresDescriptor, STATER_WRITE_SEMID);
        }
    }

    //printf("[Stater] Chiusura...\n");

    strcpy(msgZone->path, QUIT_MSG_STR);

    SIGNAL(semaphoresDescriptor, TERMINAL_READ_SEMID);
    return 0;
}

/** Funzioni Terminal **/
int terminal(size_t directoriesCount, char** paths, message_t* msgZone, int semaphoresDescriptor) {
    //printf("[Terminale] Avvio\n");

    size_t sizes[directoriesCount];

    for (size_t i = 0; i < directoriesCount; ++i)
        sizes[i] = 0;

    while(1) {
        WAIT(semaphoresDescriptor, TERMINAL_READ_SEMID);

        printf("[Terminale] Sto leggendo la memoria... (%u, %s, %lu)\n", msgZone->scannerId, msgZone->path, msgZone->size);
        if(strcmp(msgZone->path, QUIT_MSG_STR) == 0) {
            //printf("[Terminale] Messaggio d'uscita\n");
            break;
        }

        // Aggiorna il contatore
        // Va moltiplicato per 2 perchè i blocchi rilevati stat sono di dimensione 512 byte
        // Il conto risulta leggermente impreciso, da rivedere
        sizes[msgZone->scannerId] += (msgZone->size) / 2;

        SIGNAL(semaphoresDescriptor, STATER_WRITE_SEMID);
    }

    printf("\nRisultato:\n\n");
    for(size_t i = 0; i < directoriesCount; ++i) {
        printf("%s - %ld\n", paths[i], sizes[i]);
    }
    printf("\n");

    printf("[Terminale] Chiusura\n");
    return 0;
}

/** Main **/
int main(size_t argc, char** argv) {
    if(argc < 2) {
        printf("USO: %s [path-1] [path-2]\n", argv[0]);
        return 1;
    }

    // Allocazione strutture IPC
    // Memoria condivisa
    int sharedMemoryDescriptor = shmget(IPC_PRIVATE, sizeof(message_t), 0600 | IPC_CREAT | IPC_EXCL);
    if(sharedMemoryDescriptor == -1) {
        perror("sharedMemory descriptor creation");
        return -3;
    }
    message_t* msgZone = (message_t*) shmat(sharedMemoryDescriptor, NULL, 0);

    if(msgZone == (void*)-1) {
        perror("shared memory allocation");
        return -3;
    }
    // Semafori
    int semaphoresDescriptor = semget(IPC_PRIVATE, 6, 0600 | IPC_CREAT | IPC_EXCL);
    if(semaphoresDescriptor == -1) {
        perror("semaphores descriptor creation");
        return -3;
    }

    // Creazione processi
    pid_t pid;

    // Creazione processi scanner
    size_t scannersCount = argc - 1;

    for(size_t c = 0; c < scannersCount; ++c) {
        pid = fork();

        if(pid == 0)
            return scanner(c, argv[1 + c], msgZone, semaphoresDescriptor);
    }

    // Creazione processo Stater
    pid = fork();
    if(pid == 0)
        return stater(msgZone, semaphoresDescriptor, scannersCount);

    // Avvio core del processo principale
    // Sfruttando l'aritmetica dei puntatori passo l'array di stringhe che contiene i parametri da linea di comando
    // Ma ignorando la prima posizione (nome del file)
    int retcode = terminal(scannersCount, argv + 1, msgZone, semaphoresDescriptor);

    // Deallocazione strutture IPC
    if(shmctl(sharedMemoryDescriptor, IPC_RMID, NULL) != 0) {
        perror("shared memory destruction");
        return -3;
    }
 
    return retcode;
}