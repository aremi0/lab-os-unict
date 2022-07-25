/**
 * @file palindrome.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-25
 * 
 * Prova di laboratorio di SO del 2020-09-10
 *      [N.b.] negli esercizi con le pipe, specie se bisogna creare più di una pipe, E' ASSOLUTAMENTE
 *      OBBLIGATORIO chiudere nei processi nel main dei processi figli tutti i canali
 *      di tutte le pipe non utilizzati, passargli solamente il canale che gli serve e poi,
 *      prima di exit(), chiudere anche quello.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <wait.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define DIMBUF 512

void reader(int pipe, char *ptr, long size){ //pipeRP_d[1], mmap ptr, size

    for(int i = 0; i < size; i++) //scansiono e mando tutto il file mappato char-per-char
        write(pipe, &ptr[i], 1);

    //in chiusura...
    close(pipe);
    printf("\t\t[R] terminazione...\n");
    exit(0);
}

void writer(int pipe){ //pipePW_d[0]
    FILE *f;
    char buffer[DIMBUF];

    if((f = fdopen(pipe, "r")) == NULL){
        perror("fopen writer");
        exit(1);
    }

    while(fgets(buffer, DIMBUF, f))
        printf("[W] palindroma ricevuta: %s", buffer);

    //in chiusura...
    close(pipe); //chiudo canale scrittura
    fclose(f);
    printf("\t\t[W] terminazione...\n");
    exit(0);
}

int isPalindrome(char *string){
    for(int i = 0, j = strlen(string) - 2; i < j; i++, j--){ // -2 per non considerare "\n\0"
        if(string[i] != string[j])
            return 0;
    }

    return 1;
}

int main(int argc, char *argv[]){
    int pipeRP_d[2], pipePW_d[2], input_fd;
    struct stat statbuf;
    char *ptr, buffer[DIMBUF];
    FILE *_pipeRP;

    if(argc != 2){
        fprintf(stderr, "Uso: %s <input-file>\n", argv[0]);
        exit(0);
    }
    if((input_fd = open(argv[1], O_RDONLY)) == -1){ //ottengo fd del file da passare alla mmap
        perror("open");
        exit(1);
    }
    if(stat(argv[1], &statbuf) == -1){ //ottengo informazioni sul file input ==> mi serve 'size' per la mmap
        perror("stat");
        exit(1);
    }
    if((ptr = (char*)mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, input_fd, 0)) == (char*)-1){ //mappo il file input in RAM
        perror("mmap");
        exit(1);
    }
    if((pipe(pipeRP_d)) == -1){ //creo pipe Reader-Padre
        perror("pipe Reader-Padre");
        exit(1);
    }
    if((pipe(pipePW_d)) == -1){ //creo pipe Padre-Writer
        perror("pipe Padre-Writer");
        exit(1);
    }
    if((_pipeRP = fdopen(pipeRP_d[0], "r")) == NULL){ //apro la pipe del reader in uno stream
        perror("fopen padre");
        exit(1);
    }

    //creazione figli...
    if(fork() == 0){
        close(pipePW_d[0]); //writer...
        close(pipePW_d[1]); //writer...
        close(pipeRP_d[0]); //non serve
        reader(pipeRP_d[1], ptr, statbuf.st_size); //pipeRP_d[1], mmap ptr, size
    }
    if(fork() == 0){
        close(pipeRP_d[0]); //reader...
        close(pipeRP_d[1]); //reader...
        close(pipePW_d[1]); //non serve
        writer(pipePW_d[0]); //pipePW_d[0]
    }

    //chiusura canali pipe non usati dal padre...
    close(pipeRP_d[1]);
    close(pipePW_d[0]);

    while((fgets(buffer, DIMBUF, _pipeRP))){ //leggo parola-per-parola
        if(isPalindrome(buffer))
            write(pipePW_d[1], &buffer, strlen(buffer)); //scrivo parola-per-parola
    }

    close(pipeRP_d[0]);
    close(pipePW_d[1]);
    close(input_fd);
    fclose(_pipeRP);
    munmap(ptr, statbuf.st_size);
    printf("\t\t[PADRE] terminazione...\n");
    exit(0);
}