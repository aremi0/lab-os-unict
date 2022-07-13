/**
 * @file file-shell.c
 * @author aremi
 * @version 0.1
 * @date 2022-07-12
 * 
 * Prova di laboratorio di SO del 2015-07-01
 *
 * N.B. il codice presenta tutti (spero) i controlli necessari nel parsing
 * dell'input utente. Controlli che nella consegna del compito NON ERANO RICHIESTI.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define DIM_REPONSE 1024
#define DIM_REQUEST 256

typedef struct { //richieste dal padre ai figli
    long type; //<n> ==> pType
    int eof;
    char cmd[DIM_REQUEST];
    char nomeFile[DIM_REQUEST];
    char chiave[DIM_REQUEST];
    char dir[DIM_REQUEST];
} request;

typedef struct { //risposte dei figli al padre
    long type; //999 ==> per il padre
    int eof;
    char text[DIM_REPONSE];
} reponse;

void _help(int caso){ //padre ==> error msg handler
    switch(caso){
    case 1:
        printf("Digita un comando tra:\n");
        printf("$ list <n>\n");
        printf("$ size <n> <nome-file>\n");
        printf("$ search <n> <nome-file> <stringa>\n");
        printf("$ exit\n");
        break;
    case 2:
        printf("Comando: ");
        break;
    case 3:
        printf("--input error, repeat...--\n");
        _help(1);
        break;
    case 4:
        printf("--unkwown command, repeat...--\n");
        _help(1);
        break;
    }
}

/** ritorna:
*        -1: errore input
*         0: exit
*  <n>/type: altrimenti */
int parsing(char *input, request *messaggio){
    char *token;
    long tipo;

    token = strtok(input, " "); //cmd
    if(!token)
        return -1;

    if(strncmp(token, "exit", strlen("exit")) == 0){
        messaggio->type = 0;
        messaggio->eof = 1;
        return 0;
    }

    if((strcmp(token, "list") != 0) && (strcmp(token, "size") != 0) && (strcmp(token, "search") != 0))
        return -1;
    
    messaggio->eof = 0;
    strncpy(messaggio->cmd, token, strlen(token));

    token = strtok(NULL, " "); //type
    if(!token || (tipo = atol(token)) == 0) //se <n> è NULL o invalido...
        return -1;
    else
        messaggio->type = tipo;

    token = strtok(NULL, " "); //nomeFile
    if(token)
        strncpy(messaggio->nomeFile, token, strlen(token));
    else
        strncpy(messaggio->nomeFile, "-", strlen("-"));

    token = strtok(NULL, " "); //chiave
    if(token)
        strncpy(messaggio->chiave, token, strlen(token));
    else
        strncpy(messaggio->chiave, "-", strlen("-"));

    //manca la directory, verrà aggiunta nel main
    return messaggio->type;
}

void search(request richiesta, int coda){
    struct dirent *entry;
    struct stat statbuf;
    DIR *dp;
    reponse risposta;
    int riga;
    FILE *file;

    //apre la directory
    if((dp = opendir(richiesta.dir)) == NULL){
        perror("opendir");
        exit(1);
    }

    //sposto il puntatore current working directory alla directory interessata
    if(chdir(richiesta.dir) == -1){
        perror("chdir");
        exit(1);
    }

    memset(&risposta, 0, sizeof(reponse));
    risposta.type = 999;
    risposta.eof = 1;
    strcpy(risposta.text, "\tfile non trovato!");

    while((entry = readdir(dp)) != NULL){ //per ogni entry della directory...
        lstat(entry->d_name, &statbuf);
        if(strcmp(entry->d_name, richiesta.nomeFile) == 0){ //se trova il file...
            if(!S_ISREG(statbuf.st_mode)){ //...se non è un file di testo...
                strcpy(risposta.text, "\tnon e' un file di testo!\n");
                break;
            }

            if((file = fopen(entry->d_name, "r")) == NULL){ //apre il file in lettura
                perror("fopen");
                exit(1);
            }

            strcpy(risposta.text, "\toccorrenze della chiave nelle righe: [ ");
            riga = 0;
            char aux[DIM_REPONSE];
            while(fgets(aux, DIM_REPONSE, file)){ //leggo tutte le righe del file...
                riga++;
                if(strstr(aux, richiesta.chiave)){ //... se la riga contiene la chiave...
                    sprintf(aux, "%d", riga); //necessario per azziccare 'riga' (int) in un char*
                    strcat(risposta.text, aux); // ...scrivo il numero di riga del match
                    strcat(risposta.text, " ");
                }
            }
            strcat(risposta.text, "]");
            break; //dopo aver trovato il file ed averlo processato, posso uscire direttamente...
        }
    }

    if((msgsnd(coda, &risposta, strlen(risposta.text)+sizeof(int), 0)) == -1){ //lo mando al padre
        perror("msgsnd figlio");
        exit(1);
    }

    fclose(file);
}

void size(request richiesta, int coda){
    struct dirent *entry;
    struct stat statbuf;
    DIR *dp;
    reponse risposta;
    int found = 0;

    //apre la directory
    if((dp = opendir(richiesta.dir)) == NULL){
        perror("opendir");
        exit(1);
    }

    //sposto il puntatore current working directory alla directory interessata
    if(chdir(richiesta.dir) == -1){
        perror("chdir");
        exit(1);
    }

    memset(&risposta, 0, sizeof(reponse));
    risposta.type = 999;
    risposta.eof = 1;
    strcpy(risposta.text, "\tfile non trovato!\n");

    while((entry = readdir(dp)) != NULL){ //per ogni entry della directory...
        lstat(entry->d_name, &statbuf);
        if(strcmp(entry->d_name, richiesta.nomeFile) == 0){ //se trova il file...
            char aux[128];
            sprintf(aux, "%lu", statbuf.st_size); //necessario per azziccare 'statbauf->.st_size' (uns long) in un char*

            strcpy(risposta.text, "\t");
            strcat(risposta.text, entry->d_name);
            strcat(risposta.text, " => ");
            strcat(risposta.text, aux);
            strcat(risposta.text, " byte\n");

            break;
        }
    }

    if((msgsnd(coda, &risposta, strlen(risposta.text)+sizeof(int), 0)) == -1){ //lo mando al padre
        perror("msgsnd figlio");
        exit(1);
    }
}

void list(request richiesta, int coda){
    struct dirent *entry;
    struct stat statbuf;
    DIR *dp;
    reponse risposta;

    //apre la directory
    if((dp = opendir(richiesta.dir)) == NULL){
        perror("opendir");
        exit(1);
    }

    //sposto il puntatore current working directory alla directory interessata
    if(chdir(richiesta.dir) == -1){
        perror("chdir");
        exit(1);
    }

    while((entry = readdir(dp)) != NULL){ //per ogni entry della directory...
        lstat(entry->d_name, &statbuf);

        if(S_ISREG(statbuf.st_mode)){ //...se è un file regolare...
            memset(&risposta, 0, sizeof(reponse));
            risposta.type = 999;
            risposta.eof = 0;
            strcpy(risposta.text, "\t");
            strcat(risposta.text, entry->d_name);
            if((msgsnd(coda, &risposta, strlen(risposta.text)+sizeof(int), 0)) == -1){ //...lo mando al padre
                perror("msgsnd figlio");
                exit(1);
            }
        }
    }

    //mando l'eof al padre
    memset(&risposta, 0, sizeof(reponse));
    risposta.type = 999;
    risposta.eof = 1;
    if((msgsnd(coda, &risposta, sizeof(reponse)-sizeof(long), 0)) == -1){ //lo mando al padre
        perror("msgsnd figlio");
        exit(1);
    }
}

//i figli manderanno in coda un pacchetto per ogni file richiesto dal padre...
void _processo(int coda, int pType){ //process handler ==> list, size o search
    request richiesta;

    while(1){
        memset(&richiesta, 0, sizeof(request));

        if(msgrcv(coda, &richiesta, sizeof(request), pType, 0) == -1){
            perror("msgrcv figlio");
            exit(1);
        }

        if(richiesta.eof == 1){ //devo uscire...
            printf("[FIGLIO %d]==>terminazione\n", pType);
            exit(0);
        }

        if(strncmp(richiesta.cmd, "list", strlen("list")) == 0)
            list(richiesta, coda);
        else if(strncmp(richiesta.cmd, "size", strlen("size")) == 0)
            size(richiesta, coda);
        else if(strncmp(richiesta.cmd, "search", strlen("search")) == 0)
            search(richiesta, coda);
    }
}

int main(int argc, char *argv[]){
    int coda, _type = 0;
    char input[DIM_REQUEST];
    request richiesta;
    reponse risposta;

    if(argc < 2){
        printf("Uso: %s <directory_1> [<directory_n>]\n", argv[0]);
        exit(1);
    }

    //creazione coda dei messaggi
    if((coda = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0600)) == -1){
        perror("msgget");
        exit(1);
    }

    //creazione processi figli
    for(int i = 1; i < argc; i++){
        if(fork() != 0)
            _processo(coda, i);
    }
 
    _help(1);

    //ciclo del padre (shell interattiva)
    while(1) {
        memset(&richiesta, 0, sizeof(request));
        memset(&risposta, 0, sizeof(request));

        _help(2);

        //ottengo comando utente
        fgets(input, DIM_REQUEST, stdin);
        int len = strlen(input);
        input[len-1] = '\0';

        //parsing comando utente e controlli input
        _type = parsing(input, &richiesta); //_type conterrà type/<n>
        if(_type == 0){ //è stato digitato exit...
            for(int i = 1; i < argc; i++){ //manda tante EOF quanti i processi...
                richiesta.type = i;
                if((msgsnd(coda, &richiesta, sizeof(richiesta)-sizeof(long), 0)) == -1){
                    perror("msgsnd padre");
                    exit(1);
                }
            }

            msgctl(coda, IPC_RMID, NULL);
            printf("[PADRE]==>terminazione\n");
            exit(0);
        }
        else if(_type == -1 || _type > argc-1){ //è stato inserito un parametro invalido...
            _help(3);
            continue;
        }

        //tutti i controlli sono stati superati, mando la richiesta al figlio pType ma prima...
        strncpy(richiesta.dir, argv[_type], strlen(argv[_type])); //...inserisco dir
        if((msgsnd(coda, &richiesta, sizeof(request)-sizeof(long), 0)) == -1){
            perror("msgsnd padre");
            exit(1);
        }

        /*  Aspetto le risposte dei figli; nello specifico aspetto fin quando non mi mandano l'EOF  */
        while(msgrcv(coda, &risposta, sizeof(reponse), 999, 0) > 0){
            if(risposta.eof){
                printf("%s\n", risposta.text);
                break;
            }
            else
                printf("%s\n", risposta.text);
        }
    }
}