/**
 * @file palindrome.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-25
 * 
 * Prova di laboratorio di SO del 2020-09-10
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
#include <ctype.h>

#define DIMBUF 512

void reader(int *pipe, char *ptr){ //pipeRP_d, mmap ptr
    char buffer;
    close(pipe[0]); //chiudo canale lettura
    int i = 0;

    while((write(pipe[1], ptr, 1)) > 0) //scansiono e mando tutto il file mappato char-per-char
        ptr++;


    //in chiusura...
    close(pipe[1]);
    printf("\t\t[R] terminazione...\n");
    exit(0);
}

void writer(int *pipe){ //pipePW_d
    FILE *f;
    char buffer[DIMBUF];
    close(pipe[1]); //chiudo canale scrittura

    if((f = fdopen(pipe[0], "r")) == NULL){
        perror("fopen writer");
        exit(1);
    }

    while(fgets(buffer, DIMBUF, f)){
        printf("[W] palindroma ricevuta: %s", buffer);
//printf("__debug_writer_attesa_\n");
    }

    //in chiusura...
    close(pipe[0]); //chiudo canale scrittura
    fclose(f);
    printf("\t\t[W] terminazione...\n");
    exit(0);
}

int isPalindrome(char *string){
    for(int i = 0, j = strlen(string) - 2; i < j; i++, j--){
        if(string[i] != string[j])
            return 0;
    }

    return 1;
}

int main(int argc, char *argv[]){
    int pipeRP_d[2], pipePW_d[2], input_fd;
    struct stat statbuf;
    char *ptr, buffer[DIMBUF];
    FILE *f;

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
    if((pipe(pipePW_d)) == -1){
        perror("pipe Padre-Writer"); //creo pipe Padre-Writer
        exit(1);
    }
    if((f = fdopen(pipeRP_d[0], "r")) == NULL){
        perror("fopen padre");
        exit(1);
    }

    //creazione figli...
    if(fork() == 0)
        reader(pipeRP_d, ptr); //pipeRP_d, mmap ptr, size
    if(fork() == 0)
        writer(pipePW_d); //pipePW_d

    close(pipeRP_d[1]); //chiudo canale scrittura reader
    close(pipePW_d[0]); //chiudo canale lettura writer

    while((fgets(buffer, DIMBUF, f))){ //leggo parola-per-parola
printf("__debug_padre__%s", buffer);
        if(isPalindrome(buffer))
            write(pipePW_d[1], &buffer, strlen(buffer)); //scrivo parola-per-parola
    }

printf("asdasdasd__debug_writer_attesa_\n");

    close(pipeRP_d[0]);
    close(pipePW_d[1]);
    close(input_fd);
    fclose(f);
    munmap(ptr, statbuf.st_size);
    printf("\t\t[PADRE] terminazione...\n");
    exit(0);
}