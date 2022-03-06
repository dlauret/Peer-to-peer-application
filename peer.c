#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>

#define MAXBUF 256
#define MAX_CONC 1000

// creo struttura per inserire le entry
// 1 per giorno e vengono sommate le quantita' in base al tipo
struct Entry {
    char buf_date[10+1]; // AAAA/MM/GG
    int porta; // peer
    int qnt_T; // quantita' tamponi
    int qnt_N; // quantita' nuovi casi
    int lock; // per vedere se bloccare o no il registro
    struct Entry *next; // punta a quella di un giorno successivo
};

struct List_variazione {
    char buffer[50];
    struct List_variazione *next;
};
struct List_variazione *lista_var = NULL;

// funzione che inserisce un nuovo elemento in coda nella lista lista_var
// e' utilizzata per poi stampare le variazioni
void inserimento_coda_variazioni(char buf[50])
{
    struct List_variazione *new_node = NULL;
    struct List_variazione *last = NULL;

    new_node = (struct List_variazione *)malloc(sizeof(struct List_variazione));

    strcpy(new_node->buffer, buf);
    new_node->next = NULL;

    if(lista_var == NULL) {
        // se primo elemento
        lista_var = new_node;
        return;
    }

    // arrivare fino all'ultimo nodo
    last = lista_var;
    while(last->next)
        last = last->next;
    
    last->next = new_node;
}

// funzione per "azzerare" la lista lista_var
void free_list_variazione()
{
    struct List_variazione *temp;
    
    while(lista_var != NULL){
        temp = lista_var;
        lista_var = lista_var->next;
        free(temp);            
    }
}

// funzione per stampare nodi lista_var
void printList_variazione()
{
    struct List_variazione *temp;

    if(lista_var != NULL)
    {
        printf("\nVariazioni:\n");
        temp = lista_var;
        do {
            printf("%s\n", temp->buffer);
            temp = temp->next;
        } while(temp != NULL);

        printf("\n");
    }
}

// funzione che controlla se e' presente un nodo con stessa data e tipo
// cosi' dobbiamo aumentare solo la quantita'
// ret 1 se lo trova
// ret 0 altrimenti
int if_present_assign(struct Entry *start, char info_time[11], char type, int quantity, int p)
{
    struct Entry *temp = start;
    
    if(!start)
        return 0;

    while(temp != NULL) {
        if((strcmp(temp->buf_date, info_time) == 0) /* se data uguale */
            && (temp->porta == p)) /* se porta (peer) uguale*/
        {
            // allora aumentiamo quantita'
            // in base al tipo
            if(type == 'T') // tamponi
                temp->qnt_T = temp->qnt_T + quantity;
            else if(type == 'N') // nuovi casi
                temp->qnt_N = temp->qnt_N + quantity;
            // gestire caso else (errore)

            return 1;
        }
        else
            temp = temp->next;
    }

    // se arriva qui vuol dire non trovato ---> ret 0
    return 0;
}

// inserimento di una entry: inserimento IN TESTA
void insert_entry(struct Entry** head_ref, struct Entry* new_node)
{
    
    // dobbiamo controllare se c'e' una entry con stessa data e stessa porta
    // in questo caso lo IGNORIAMO SE la quantita' della entry <=
    // infatti vorrebbe dire che e' un duplicato, oppure non aggiornata
    if(*head_ref){
        // se e' presente almeno un elemento
        struct Entry* temp = *head_ref;

        while(temp != NULL) {
            if((strcmp(temp->buf_date, new_node->buf_date) == 0) /* se data uguale */
                && (temp->porta == new_node->porta)) /* se porta (peer) uguale*/
            {
                // se stessa data e stesso peer
                // se new_node->qnt <= temp->qnt
                if(new_node->qnt_N > temp->qnt_N)
                    temp->qnt_N = new_node->qnt_N;
                
                if(new_node->qnt_T > temp->qnt_T)
                    temp->qnt_T = new_node->qnt_T;
                
                return; // non dobbiamo aggiungere nulla
            }
            else
                temp = temp->next;
        }

    }

   // se arriviamo qui vuol dire che non e' presente una entry con data e porta uguale    
    new_node->next = *head_ref;
    *head_ref = new_node;
    
}

// funzione per stampare nodi lista entry
void printList(struct Entry *start)
{
    struct Entry *temp;
    
    if(start != NULL)
    {
        printf("\nENTRIES:\n");
        printf("Data\t\tPta\tqntT\tqntN\tlock\n");
        temp = start;
        do {
            printf("%s\t%d\t%d\t%d\t%d\n", temp->buf_date, temp->porta, temp->qnt_T, temp->qnt_N, temp->lock);
            temp = temp->next;
        } while(temp != NULL);

        printf("\n");
    }
}

// funzione che data in ingresso una stringa la divide (se e' presente)
// la divide quando incontra il carattere '-'
void dividePeriod(char *buf, char *startPeriod, char *endPeriod) // controllare
{
    int i = 0;
    // assegnamo la prima data (o *) a startPeriod
    while(buf[i] != '-')
    {
        startPeriod[i] = buf[i];
        i += 1;
    }
    i++;
    // assegnamo la seconda data (o *) a endPeriod
    sprintf(endPeriod, "%s", buf+i);   
}
// funzione che cerca l'aggregazione nel file my_aggregazione.txt del peer
// FORMATO FILE: Aggregazione Type DataInizio DataFine
// myport serve per entrare nella cartella
// return -> il valore dell'aggregazione richiesta (nel caso Totale), ritorna 0 nel caso variazione
//
// nel caso variazione copia in lista_var una riga per elemento dal file my_aggregazioni
int find_aggr(char aggr, char type, char startPeriod[11], char endPeriod[11], int myport)
{
    char buffer[MAXBUF];
    FILE *fptr;
    
    // stesse operazioni del boot
    sprintf(buffer, "%d", myport);
    if(chdir(buffer)){
        printf("Aggregazione non calcolata precedentemente, no cartella\n");
        return -1;    
    }
    else {
        
        fptr = fopen("my_aggregazioni.txt", "r");
        if(fptr == NULL){
            printf("Aggregazione non calcolata precedentemente, no file\n");
            chdir("../");
            return -1;
            
        }
        else {
            
            char riga[100];
            char *retF;
            char temp_aggr, temp_type;
            char buf[50];
            char dataIni[11], dataFin[11];
            int risultato;
            while(1){
                retF = fgets(riga, 100, fptr);
                if(retF == NULL) // cioe' EOF
                    break;
                
                sscanf(riga,"%c %c %s %s %d", &temp_aggr, &temp_type, dataIni, dataFin, &risultato);
                
                //sprintf(buf,"%c %c %s %s %d", temp_aggr, temp_type, dataIni, dataFin, risultato);
                //inserimento_coda_variazioni(buf);

                // TOTALE
                if( (temp_aggr == aggr) && (temp_aggr == 'T')
                    && (temp_type == type)
                    && (strcmp(dataIni, startPeriod) == 0)
                    && (strcmp(dataFin, endPeriod) == 0) )
                {
                    printf("\nAggregazione trovata!\n");
                    fclose(fptr);
                    chdir("../");
                    return risultato;
                }

                //VARIAZIONE
                if( (temp_aggr == aggr) && (temp_aggr == 'V')
                    && (temp_type == type)
                    && (strcmp(dataIni, startPeriod) == 0)
                    && (strcmp(dataFin, endPeriod) == 0) )
                {
                    printf("\nAggregazione trovata!\n");
                    // inserisce nella lista lista_var un elemento per ogni riga incontrata
                    do {
                        retF = fgets(riga, 100, fptr);
                        if(retF == NULL) // cioe' EOF
                            break;
                        // in questo caso dataIni corrisponde al giorno dopo e dataFin al giorno prima
                        sscanf(riga,"%c %c %s %s %d", &temp_aggr, &temp_type, dataIni, dataFin, &risultato);
                        if(temp_aggr != 'f'){
                            //sprintf(buf,"%c %c %s %s %d", temp_aggr, temp_type, dataIni, dataFin, risultato);
                            sprintf(buf,"%s - %s = %d\n", dataIni, dataFin, risultato);
                            inserimento_coda_variazioni(buf);
                        }
                        

                        // termina quando incontra f come primo carattere
                    }while(temp_aggr != 'f');
                    
                    
                    fclose(fptr);
                    chdir("../"); 
                    return 0;
                }
            }
            // se non lo trova
            fclose(fptr);
        }
        chdir("../");  
    }
    return -1;
}

// funzione che ritorna il numero di entryes in un determinato periodo del tipo specificato
// usata per la get, quando si confrontano il numero che ritorna questa funzione con quelle inviate dal server
// qui non facciamo il controllo sul lock perche' lo fa il DS quando ci invia il suo numero di entry
int conta_entries(struct Entry *start, char type, char startPeriod[11], char endPeriod[11])
{
    if(start == NULL)
        return 0;
    
    struct Entry *temp = start;
    int counter = 0;
    // casi:
    // 1) startPeriod = *  --> controllo solo <= endPeriod
    // 2) startPeriod e endPeriod sono due date normali
    // In tutti devo controllare se la entri ha lock = 1


    // 1)
    if(startPeriod[0] == '*')
    {
        while (temp != NULL)
        {
            // se e' locked e data temp e' minore(=) di endPeriod
            if(strcmp(temp->buf_date, endPeriod) <= 0)
            {
                if(type == 'T') {
                    if(temp->qnt_T > 0)
                        counter = counter + 1;
                }
                else if(type == 'N') {
                    if(temp->qnt_N > 0)
                        counter = counter + 1;
                }
                
            }

            temp = temp->next;
        }
    }
    // 2)
    else
    {
        // cioe' date normali -> data temp >= startP e <= endP
        while (temp != NULL)
        {
            if((strcmp(temp->buf_date, startPeriod) >= 0)
                && (strcmp(temp->buf_date, endPeriod) <= 0))
            {

                if(type == 'T') {
                    if(temp->qnt_T > 0)
                        counter = counter + 1;
                }
                else if(type == 'N') {
                    if(temp->qnt_N > 0)
                        counter = counter + 1;
                }
            }

            temp = temp->next;
        }        
    }

    return counter;
}


// funzione che ritorna il TOTALE di quantita del tipo specificato in un determinato periodo
int calcolo_totale(struct Entry *start, char type, char startPeriod[11], char endPeriod[11])
{
    struct Entry *temp = start;
    int contN = 0; // cont casi
    int contT = 0; // cont tamponi
    // casi:
    // 1) startPeriod = *  --> controllo solo <= endPeriod
    // 2) startPeriod e endPeriod sono due date normali
    // mantengo due contatori e poi restituisco solo quello richiesto

    // 1)
    if(startPeriod[0] == '*')
    {
        while (temp != NULL)
        {
            // se e' locked e data temp e' minore(=) di endPeriod
            if(strcmp(temp->buf_date, endPeriod) <= 0)
            {
                contT = contT + temp->qnt_T;
                contN = contN + temp->qnt_N;
            }

            temp = temp->next;
        }
    }
    // 2)
    else
    {
        // cioe' date normali -> data temp >= startP e <= endP
        while (temp != NULL)
        {
            if((temp->lock == 1) && (strcmp(temp->buf_date, startPeriod) >= 0)
                && (strcmp(temp->buf_date, endPeriod) <= 0))
            {
                contT = contT + temp->qnt_T;
                contN = contN + temp->qnt_N;
            }

            temp = temp->next;
        }        
    }

    if(type == 'T')
        return contT;
    else if(type == 'N')
        return contN;
    else
        return -1;
    
}

// funzione per aggiungere 'days' giorni alla data passata come parametro
void addDay(struct tm *date, const int days)
{
    date->tm_year -= 1900;
    date->tm_mon -= 1;
    date->tm_mday += days;
    mktime(date);
    date->tm_year += 1900;
    date->tm_mon += 1;
}

// funzione che trova la data minore presente nella lista di entry
void find_data_minore(struct Entry *start, char *data_minore)
{
    struct Entry *temp = start;
    if(!start)
        return;

    while(temp != NULL)
    {
        if(strcmp(temp->buf_date, data_minore) < 0){
            sprintf(data_minore,"%s", temp->buf_date); // diventa la nuova data minore
        } 
        temp = temp->next;
    }
}

// funzione che calcola e stampa l'aggregazione "variazione"
// formato dell'aggregazione 'variazione' su file:
// V type dataIni dataFin 0
// - - data_succ data_corr valore
// ripetizione riga precedente per le date target
// f - - - 0
// fine formato aggregazione su file
void calcolo_variazione(struct Entry *start, char type, char startPeriod[11], char endPeriod[11],int porta)
{
    struct tm tm_time;
    time_t now;
    now = time(NULL);
    tm_time = *localtime(&now);

    char current_day[11];
    char next_day[11];
    int current_variazione = 0;
    char temp[30];
    int ini_asterisco = 0; // per capire se c'era un asterisco come startPeriod --> nel file salvo l'*

    FILE *fptr;
    if(startPeriod[0] == '*')
    {
        // trova la data minore
        char data_minore[11];
        ini_asterisco = 1;
        strcpy(data_minore, endPeriod);
        find_data_minore(start, data_minore);
        strcpy(startPeriod, data_minore);
    }
    // adesso startPeriod se era '*' e' sostituita con la data minore presente
    if(startPeriod[0] != '*')
    {
        strcpy(current_day, startPeriod);
        strcpy(next_day, startPeriod);
        printf("\nVariazioni %c:\n", type);
        // SALVATAGGIO SU FILE
            sprintf(temp, "%d", porta);
            
            if(chdir(temp)) {
                // se non c'e' la cartella
                mkdir(temp, S_IRWXU|S_IRWXG|S_IROTH|S_IRWXO);
                chdir(temp);
            }
            
            
            fptr = fopen("my_aggregazioni.txt", "a");
            if(fptr == NULL)
                printf("Error opening file my_aggregazioni\n");
            else{
                if(ini_asterisco == 1)
                    fprintf(fptr, "V %c * %s 0\n", type, endPeriod);
                else
                    fprintf(fptr, "V %c %s %s 0\n", type, startPeriod, endPeriod);
            }
        do{
            // calcolo next_day
            sscanf(next_day, "%d/%02d/%02d", &tm_time.tm_year, &tm_time.tm_mon, &tm_time.tm_mday);
            addDay(&tm_time, +1);
            sprintf(next_day, "%d/%02d/%02d", tm_time.tm_year, tm_time.tm_mon, tm_time.tm_mday);

            // calcolo il totale del giorno dopo - totale del giorno corrente
            current_variazione = calcolo_totale(start, type, next_day, next_day) - calcolo_totale(start, type, current_day, current_day);
            printf("%s - %s = %d\n", next_day, current_day, current_variazione);

            fprintf(fptr, "- - %s %s %d\n", next_day, current_day, current_variazione);   
            
            // Fine salvataggio current_variazione su file
            
            // aggiorno current_day
            sscanf(current_day, "%d/%02d/%02d", &tm_time.tm_year, &tm_time.tm_mon, &tm_time.tm_mday);
            addDay(&tm_time, +1);
            sprintf(current_day, "%d/%02d/%02d", tm_time.tm_year, tm_time.tm_mon, tm_time.tm_mday);

        }while(strcmp(next_day, endPeriod) < 0);
        
        // ultima riga da inserire per far capire fine inserimento variazione
        fprintf(fptr, "f - - - 0\n");

        fclose(fptr);
        chdir("../");
        
        // FINE SALVATAGGIO SU FILE

    }
}



int main(int argc, char* argv[]) {
    int ret, nbytes;
    char buffer[MAXBUF];
    char buffer_conc_porta[MAX_CONC] = " "; // buffer per concatenare porte (FLOODING)

    struct sockaddr_in server_addr; // per il server
    struct sockaddr_in neigh_addr; // per un vicino o in generale peer
    
    // variabili per struct
    struct Entry *start = NULL;
    
    struct Entry *temp;

    // questa variabile serve perche' quando dobbiamo calcolare l'aggregazione nel ramo
    // sd_ricezione_peer dopo aver ricevuto tutte le entry, non sappiamo che aggregazione dovevamo calcolare
    char aggregazione_flooding;
    
    int len = sizeof(server_addr);

    // puntatore a file
    FILE *fptr;

    // VICINI
    unsigned int neighLeft, neighRight;


    // per SELECT
    fd_set master, read_fds;
    int fdmax; // num max di descrittori

    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    FD_SET(fileno(stdin), &master);

    fdmax = fileno(stdin);

    int i;

    // usato per sapere se un vicino ha trovato il dato di aggregazione
    int dato_aggregazione_vicino = 0;


    // *********creazione socket UDP**********
    int sd_boot = -1;
    sd_boot = socket(AF_INET, SOCK_DGRAM, 0);

    // dichiarazione variabile ip e porta del server
    char ip_server[16];
    unsigned int porta_server;
    
    // settare porta del peer
    // con la porta ricevuta in input
    struct sockaddr_in my_addr;
    my_addr.sin_family = AF_INET;
    unsigned int porta_peer = atoi(argv[1]);
    my_addr.sin_port = htons(porta_peer); // porta del PEER
    my_addr.sin_addr.s_addr = INADDR_ANY;

    
    // ************* fine UDP ****************

    // **************** TCP1 ***********
    int sd_tcp = -1;


    //ret = bind(sd_tcp, (struct sockaddr*)&server_addr, sizeof(server_addr));
    // **************** fine TCP1 ****************

 

    // ******************* BOOT ****************

    // CARICARE IN MEMORIA LE ENTRY SE SONO PRESENTI NEL FILE
    sprintf(buffer, "%s", argv[1]);
    if(chdir(buffer))
        printf("File da cui caricare entry non presente\n");
    else {        
        
        fptr = fopen("my_entries.txt", "r");
        if(fptr == NULL)
            printf("File da cui caricare entry non presente\n");
        else {
            char riga[100];
            char *retF;

            while(1){
                retF = fgets(riga, 100, fptr);
                if(retF == NULL) // cioe' EOF
                    break;
                
                temp = (struct Entry *)malloc(sizeof(struct Entry));     
                sscanf(riga,"%s %d %d %d %d", temp->buf_date, &temp->porta, &temp->qnt_T, &temp->qnt_N, &temp->lock);                 
                insert_entry(&start, temp);    
                
            }
            
            fclose(fptr);
            printList(start);
        }
        chdir("../");
        
        
    }
    // FINE CARICAMENTO ENTRY

    // primo comando: start del peer
    char prima_str[6];
    do{
        printf("Comando iniziale per attivare il peer:\n1) start DS_addr DS_port\n");
        if(fgets(buffer, MAXBUF, stdin) == NULL)
            perror("\nERRORE fgets");
                    
        // salviamo i valori di interesse nelle corrispondenti variabili
        
        sscanf(buffer, "%s %s %d", prima_str, ip_server, &porta_server);

    } while(strcmp(prima_str, "start") != 0); // cioe' fin quando non si effettua comando start
    
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(porta_server); // porta del SERVER
    inet_pton(AF_INET, ip_server, &server_addr.sin_addr);
    // fine creazione indirizzo server

    // per gestire timeout
    fd_set boot_fds; // fd set per il boot
    FD_ZERO(&boot_fds);
    FD_SET(sd_boot, &boot_fds);
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    while(1) { 
        printf("\nBOOT phase\nInvio messaggio UDP al DS\n");
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        // inviamo il DS la nostra porta
        strcpy(buffer, argv[1]);
        nbytes = sendto(sd_boot, buffer, MAXBUF, MSG_WAITALL, (struct sockaddr*)&server_addr, sizeof(server_addr));
        if(nbytes < 0) /* error handing */
            perror("ERROR sendto");
        
        int n = select(sd_boot+1, &boot_fds, NULL, NULL, &timeout);
        if(n < 0)
            perror("ERRORE SELECT: ");
        else if(n == 0)
            FD_SET(sd_boot, &boot_fds);
        else
            break;
    }
    // fine gestione timeout ed invio boot
    
    // ricezione ACK
    nbytes = recvfrom(sd_boot, buffer, MAXBUF, MSG_WAITALL, (struct sockaddr*)&server_addr, (socklen_t*)&len);
    if(nbytes < 0) /* error handing */
    
        perror("\nERROR in sendto() during BOOT\n");
    else {
        printf("\nACK ricevuto\n");
    }
    
    // Ricezione dei due vicini UDP
    // SX
    nbytes = recvfrom(sd_boot, buffer, MAXBUF, MSG_WAITALL, (struct sockaddr*)&server_addr, (socklen_t*)&len);
    if(nbytes < 0) /* error handing */
        perror("\nERROR in recvfrom() during BOOT\n");
    else {
        neighLeft = atoi(buffer);
        printf("\nVicino sx ricevuto: %d\n", neighLeft);
    }

    // DX
    nbytes = recvfrom(sd_boot, buffer, MAXBUF, MSG_WAITALL, (struct sockaddr*)&server_addr, (socklen_t*)&len);
    if(nbytes < 0) /* error handing */
        perror("\nERROR in recvfrom() during BOOT\n");
    else {
        neighRight = atoi(buffer);
        printf("\nVicino dx ricevuto: %d\n", neighRight);
    }
    // fine ricezione vicini

    close(sd_boot);
    sd_boot = -1;

    // ********************************************
    // INSTAURAZIONE CONNESSIONE TCP CON SERVER
    // socket: sd_tcp
    sd_tcp = socket(AF_INET, SOCK_STREAM, 0);
    
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(porta_server); // porta del SERVER
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // assegno indirizzo al peer
    my_addr.sin_family = AF_INET;
    porta_peer = atoi(argv[1]);
    my_addr.sin_port = htons(porta_peer); // porta del PEER
    my_addr.sin_addr.s_addr = INADDR_ANY;

    // settare la porta da input
    ret = bind(sd_tcp, (struct sockaddr*)&my_addr, sizeof(my_addr));


    printf("\nATTESA TCP RESPONSE DA SERVER");
    ret = connect(sd_tcp, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if(ret < 0) {
        perror("ERRORE");
    }
    printf("\n\nTCP SD OK!\n");
    strcpy(buffer, argv[1]);
    ret = send(sd_tcp, buffer, MAXBUF, MSG_WAITALL);

    // fd aggiunto al set
    FD_SET(sd_tcp , &master);
    if(sd_tcp > fdmax)
        fdmax = sd_tcp;

    // FINE INSTAURO CONNESSIONE

    // **************** FINE BOOT **************

    // socket TCP che serve per contattare i vicini
    // usato nella exit (per ora)
    int sd_tcp_vicini = -1;

    // socket TCP usato per l'ascolto di peer
    int sd_ricezione_peer = -1;

    // **************** SOCKET PER LA RICEZIONE DA ALTRI PEER ****************
    sd_ricezione_peer = socket(AF_INET, SOCK_STREAM, 0);
    
    // assegno ulteriore indirizzo al peer
    struct sockaddr_in my_addr2;
    memset(&my_addr2, 0, sizeof(my_addr2));
    my_addr2.sin_family = AF_INET;
    porta_peer = atoi(argv[1]) - 100;
    my_addr2.sin_port = htons(porta_peer); // porta del PEER
    my_addr2.sin_addr.s_addr = INADDR_ANY;

    ret = bind(sd_ricezione_peer, (struct sockaddr*)&my_addr2, sizeof(my_addr2));
    ret = listen(sd_ricezione_peer, 50);

    FD_SET(sd_ricezione_peer, &master);
    if(sd_ricezione_peer > fdmax)
        fdmax = sd_ricezione_peer;
    
    // ************************************

    // variabili per gestire la data e l'ora
    time_t rawtime;
    struct tm *info_time;
    time(&rawtime);
    info_time = localtime(&rawtime);


    while(1) {

        read_fds = master;

        // controllare se sono le 18 per chiudere il registro odierno, se presente
        time(&rawtime);
        info_time = localtime(&rawtime);
        
        if(info_time->tm_hour >= 18)
        {
            struct Entry *aux = start;
            if(aux) // se sono presenti entry
            {
                char data_corrente[11];
                sprintf(data_corrente, "%d/%02d/%02d", info_time->tm_year+1900, info_time->tm_mon+1, info_time->tm_mday);
                while(aux != NULL)
                {
                    // controllo se data corrisponde ad oggi e se la entry e' aperta 
                    if((strcmp(data_corrente, aux->buf_date) == 0) && (aux->lock == 0)) {
                        aux->lock = 1; // chiudiamo la entry
                    }
                    aux = aux->next;
                }
            }
        }
        // fine controllo se l'ora >= 18:00


        // MENU
        printf("\n");
        puts("Seleziona un comando: \n2) add type quantity\n3) get aggr{T or V} type{T or N} period{yyyy1/mm1/dd1-yyyy2/mm2/dd2}\n4) stop:");
        
        int n = select(fdmax + 1, &read_fds, NULL, NULL, NULL);
        if(n < 0)
            perror("ERRORE SELECT: ");
                    
        
        for(i = 0; i <= fdmax; i++)
        {
            if(FD_ISSET(i, &read_fds))
            {
                // ricevo messaggio dal DS
                if(i == sd_tcp)
                {
                    // Ricevo nel buffer il tipo di messaggio che ricevero'
                    ret = recv(i, buffer, MAXBUF, MSG_WAITALL);
                    if(ret < 0)
                        perror("\nErrore recv: ");
                    
                    // UPDATE_NEIGHBORS
                    if(strcmp(buffer, "UPDATE_NEIGHBORS") == 0) { // se uguali
                       printf("\nAggiornamento vicini\n");
                        // il peer deve aggiornare i vicini
                        // ricevera' due messaggi contenenti i suoi due nuovi vicini
                        // SX
                        ret = recv(i, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0) /* error handing */
                            perror("\nERROR in recv() during UPDATE_NEIGHBORS\n");
                        else {
                            neighLeft = atoi(buffer);
                            printf("\nNEW vicino sx ricevuto: %u\n", neighLeft);
                        }

                        // DX
                        ret = recv(i, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0) /* error handing */
                            perror("\nERROR in recv() during UPDATE_NEIGHBORS\n");
                        else {
                            neighRight = atoi(buffer);
                            printf("NEW vicino dx ricevuto: %u\n", neighRight);
                        }
                    } // end if UPDATE_NEIGHBORS
                    // se siamo PRIMO peer -> RICEVIAMO ENTRIES
                    else if(strcmp(buffer, "FIRST_ENTRIES") == 0) {

                        ret = recv(i, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("\nErrore recv entries da server: ");

                        // START_ENTRIES
                        if(strcmp(buffer, "START_ENTRIES") == 0) 
                        {
                            printf("\nRicezione entry init");
                            do{

                                ret = recv(i, buffer, MAXBUF, MSG_WAITALL);
                                if(ret < 0)
                                    perror("\nErrore recv entries da server: ");
                                
                                if(strcmp(buffer, "END_ENTRIES") != 0){
                                    temp = (struct Entry *)malloc(sizeof(struct Entry));

                                    sscanf(buffer, "%s %d %d %d %d", temp->buf_date, &temp->porta, &temp->qnt_T, &temp->qnt_N, &temp->lock);
                                    insert_entry(&start, temp);                          
                                }

                            }while(strcmp(buffer, "END_ENTRIES") != 0);
                            // stampa entry
                            printList(start);
                        }
                        // FINE ENTRIES                        

                    } // end buffer == FIRST_ENTRIES

                    else if(strcmp(buffer, "CLOSE") == 0) {
                        printf("\nRESPONSE_CLOSE\n");
                        printf("\nTerminazione peer da parte del server...\n");
                        // Non dobbiamo inviare entry, rispondiamo semplicemente con un messaggio e chiudiamo porta tcp server
                        strcpy(buffer, "RESPONSE_CLOSE");
                        ret = send(sd_tcp, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("ERRORE send RESPONSE_CLOSE: ");
                        
                        // chiusura porta tcpr
                        close(sd_ricezione_peer);
                        close(sd_tcp);
                        close(sd_tcp_vicini);
                        sd_tcp = -1;
                        sd_ricezione_peer = -1;
                        sd_tcp_vicini = -1;
                        return 0;
                    }

                } // end if i == sd_tcp
                // stdin
                else if(i == fileno(stdin)) 
                {

                    if(fgets(buffer, MAXBUF, stdin) == NULL)
                        perror("\nERRORE fgets");
                    
                    // controntiamo il comando immesso con la lista di comandi

                    // ADD TYPE QUANTITY
                    if(strncmp("add ", buffer, 4) == 0)
                    {
                        // puo' inserire nel giorno corrente solo entro le 18
                        // altrimenti si passa come data al giorno successivo
                        char type;
                        int quantity;
                        
                        // salviamo i valori di interesse nelle corrispondenti variabili
                        sscanf(buffer, "%*3s %c %d", &type, &quantity);

                        
                        // controllare se sono passate le 18 --> il campo temp->buf_date riporterà il giorno successivo
                        // info_time calcolato prima del select
                        char buf_time[11];
                        if(info_time->tm_hour >= 18) {
                            time_t tomorrow;
                            tomorrow = time(NULL) + 86400 * 1;
                            info_time = localtime(&tomorrow);
                        }
                        // copia in buf_time i campi che servono dalla struttura info_time
                        sprintf(buf_time,"%d/%02d/%02d", info_time->tm_year + 1900, info_time->tm_mon + 1, info_time->tm_mday);


                        // funzione che vede se e' presente un nodo con la data di oggi
                        // con lo stesso tipo così da aumentarne la qnt
                        ret = if_present_assign(start, buf_time, type, quantity, atoi(argv[1]));
                        if(ret == 0) 
                        {
                            // se non e' presente, e' il primo del giorno
                            // dobbiamo assegnare la quantita' al tipo corretto
                            // e inseriamo la nuova entry nella lista
                            temp = (struct Entry *)malloc(sizeof(struct Entry));
                            strcpy(temp->buf_date, buf_time);

                            if(type == 'T')
                            {
                                temp->qnt_N = 0;
                                temp->qnt_T = quantity;
                            }
                            else if(type == 'N')
                            {
                                temp->qnt_N = quantity;
                                temp->qnt_T = 0;
                            }
                            
                            temp->porta = atoi(argv[1]);
                            temp->lock =  0; // settiamo APERTA la entry
                            insert_entry(&start, temp);
                        }
                        // stampa lista entry
                        printList(start);

                        // ************************************
                        // dobbiamo inviare al DS la entry
                        strcpy(buffer, "SEND_ENTRY");
                        ret = send(sd_tcp, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("ERRORE send SEND_ENTRY: ");
                        
                        sprintf(buffer, "%s %c %d %d", buf_time, type, quantity, atoi(argv[1]));
                        ret = send(sd_tcp, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("ERRORE");

                        printf("\nEntry inviata al server\n");
                        // fine invio al server della entry

                    } // end ADD
                    // GET AGGR TYPE PERIOD
                    else if(strncmp("get ", buffer, 4) == 0)
                    {
                        char aggr, type;
                        char period[40] = "";
                        int val_aggr = 0;
                                                
                        // salviamo i valori di interesse nelle corrispondenti variabili
                        sscanf(buffer, "%*3s %c %c %s", &aggr, &type, period);
                        //printf("\nPERIOD: %s\n", period);
                        char startPeriod[11];
                        char endPeriod[11];
                        if(strlen(period) == 0) {
                            strcpy(startPeriod, "*");
                            strcpy(endPeriod, "*");
                        }
                        else {
                            strcpy(startPeriod, " ");
                            strcpy(endPeriod, " ");
                            dividePeriod(period, startPeriod, endPeriod);
                        }
                        // adesso in startPeriod e endPeriod ho inizio e fine data
                        
                        // azzeriamo la lista variazione
                        free_list_variazione();

                        // vedere se abbiamo salvato nel file questa aggregazione
                        // restituisce il valore dell'aggregazione
                        // nel caso di variazione, inserisce i risultati nella lista lista_var
                        val_aggr = find_aggr(aggr, type, startPeriod, endPeriod, atoi(argv[1]));
                        
                        
                        if(val_aggr == -1) // se NON lo abbiamo trovato
                        {
                            // DOMANDARE AL SERVER QUANTE ENTRY CI SONO NEL PERIOD
                            strcpy(buffer, "HOW_MANY_ENTRIES");
                            ret = send(sd_tcp, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("ERRORE send how many entries: ");

                            // dobbiamo spedirgli il tipo e il periodo: type, startPeriod, endPeriod
                            sprintf(buffer, "%c %s %s", type, startPeriod, endPeriod);
                            ret = send(sd_tcp, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("ERRORE send how many entries: ");

                            // SE endPeriod = '*'
                            // ALLORA ci aspettiamo un messaggio dal server che ci comunica la data più recente con lock = 1.
                            if(strcmp(endPeriod, "*") == 0) {
                                ret = recv(sd_tcp, buffer, MAXBUF, MSG_WAITALL);
                                if(ret < 0)
                                    perror("ERRORE recv how many entries: ");
                                printf("\nData maggiore sostituita al '*'");

                                // copiamo su endPeriod la data maggiore con lock = 1
                                strcpy(endPeriod, buffer);
                                printf("\nNuovo endPeriod: %s", endPeriod);
                            }

                            // Riceviamo numero di entries dal server
                            ret = recv(sd_tcp, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("ERRORE recv how many entries: ");
                            puts("\nNumero di entry ricevute\n");
                            // trasformiamo in intero il valore ricevuto
                            int how_many_server = atoi(buffer); // valore del server

                            // Se ci arriva -1, vuol dire che sono presenti delle entry nel nostro intervallo
                            // che non sono chiuse (cioe' hanno lock = 0)
                            if(how_many_server == -1){
                                puts("ERRORE: Nell'intervallo scelto sono presenti entry non chiuse!\nAggregazione non calcolabile\n");
                                break;
                            }

                            // calcoliamo quante ne abbiamo NOI
                            ret = conta_entries(start, type, startPeriod, endPeriod);            
                      
                            // SE ABBIAMO LO STESSO NUMERO DI ENTRY RICEVUTO (nel periodo)
                            // 1) allora -> calcoliamo aggregazione,
                            // 2) altrimenti -> CONTATTIAMO I VICINI

                            if(ret == how_many_server) { // 1)
                                printf("\nNumero delle entry e' uguale a quelle del server: %d, possiamo calcolare l'aggegazione\n", how_many_server);
                                int risultato = 0;
                                // CALCOLO TOTALE
                                if(aggr == 'T') {
                                    risultato = calcolo_totale(start, type, startPeriod, endPeriod);
                                    printf("\nTOTALE: %d\n", risultato);
                                    // SALVATAGGIO SU FILE del TOTALE
                                    sprintf(buffer, "%s", argv[1]);
                                    
                                    if(chdir(buffer)) {
                                        // se non c'e' la cartella
                                        mkdir(buffer, S_IRWXU|S_IRWXG|S_IROTH|S_IRWXO);
                                        chdir(buffer);
                                    }

                                    fptr = fopen("my_aggregazioni.txt", "a");
                                    if(fptr == NULL)
                                        printf("Error opening file my_aggregazioni\n");
                                    else {
                                        fprintf(fptr, "%c %c %s %s %d\n", aggr, type, startPeriod, endPeriod, risultato);

                                        fclose(fptr);
                                    }
                                    chdir("../");
                                    
                                    

                                    // FINE SALVATAGGIO SU FILE
                                }
                                // CALCOLO VARIAZIONE
                                else if(aggr == 'V') {
                                    printf("\nSto calcolando la variazione");
                                    calcolo_variazione(start, type, startPeriod, endPeriod, atoi(argv[1]));

                                }
                                
                            // ALTRIMENTI dobbiamo chiedere ai vicini
                            // per vedere se hanno l'aggregazione richiesta
                            }
                            else { // 2)

                                // CONTATTO I VICINI ---> REQ_DATA
                                printf("\nContatto i vicini\n");
                                // ATTIVARE CONNESSIONE TCP CON VICINO SX
                                sd_tcp_vicini = socket(AF_INET, SOCK_STREAM, 0);

                                // assegno indirizzo nostro
                                my_addr.sin_family = AF_INET;
                                porta_peer = atoi(argv[1])+100; // Dobbiamo usare un'altra porta
                                my_addr.sin_port = htons(porta_peer); // porta del PEER
                                my_addr.sin_addr.s_addr = INADDR_ANY;

                                // indirizzo del peer vicino di SX
                                memset(&neigh_addr, 0, sizeof(neigh_addr));
                                neigh_addr.sin_family = AF_INET;
                                neigh_addr.sin_port = htons(neighLeft-100); // porta del VICINO DI SX (quella che ascolta)
                                neigh_addr.sin_addr.s_addr = INADDR_ANY;

                                // settare la porta da input
                                ret = bind(sd_tcp_vicini, (struct sockaddr*)&my_addr, sizeof(my_addr));
                                
                                printf("\nATTESA TCP RESPONSE DA VICINO SX");
                                ret = connect(sd_tcp_vicini, (struct sockaddr*)&neigh_addr, sizeof(neigh_addr));
                                if(ret < 0) {
                                    perror("ERRORE");
                                }
                                printf("\n\nTCP VICINO SX OK!\n");

                                strcpy(buffer, "REQ_DATA");
                                ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                if(ret < 0)
                                    perror("ERRORE");

                                // dobbiamo inviare le variabili per capire se ha STESSA aggr nel file
                                // le variabili da inviare sono quelle che servono per la funzione find_aggr
                                // quindi: aggr, type, startPeriod, endPeriod
                                sprintf(buffer, "%c %c %s %s", aggr, type, startPeriod, endPeriod);
                                ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                if(ret < 0)
                                    perror("ERRORE");
                                printf("\nInvio variabili per capire se ha l'aggregazione\n");
                                
                                ret = recv(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                if(ret < 0)
                                    perror("ERRORE");
                                // ricezione SI_DATA e NO_DATA
                                if(strcmp(buffer, "SI_DATA") == 0)
                                {
                                    printf("\nIl vicino ha inviato l'aggregazione!\n");
                                    dato_aggregazione_vicino = 1;
                                    
                                    // CASO ***** TOTALE *****
                                    if(aggr == 'T')
                                    {
                                        ret = recv(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                        if(ret < 0)
                                            perror("ERRORE");
                                    
                                        printf("\nRisultato: %d\n", atoi(buffer));

                                        // SALVATAGGIO SU FILE del TOTALE
                                        sprintf(buffer, "%s", argv[1]);
                                        
                                        if(chdir(buffer)) {
                                            // se non c'e' la cartella
                                            mkdir(buffer, S_IRWXU|S_IRWXG|S_IROTH|S_IRWXO);
                                            chdir(buffer);
                                        }
                                        
                                        fptr = fopen("my_aggregazioni.txt", "a");
                                        if(fptr == NULL)
                                            printf("Error opening file my_aggregazioni\n");
                                        else {    
                                            
                                            fprintf(fptr, "%c %c %s %s %d\n", aggr, type, startPeriod, endPeriod, atoi(buffer));
                                        
                                            fclose(fptr);
                                            
                                        }
                                        chdir("../");
                                        
                                       
                                        // FINE SALVATAGGIO SU FILE

                                    } // end caso totale

                                    // CASO ***** VARIAZIONE *****
                                    else if(aggr == 'V')
                                    {
                                        // salva su file e stampa a video
                                        char temp[30];
                                        sprintf(temp, "%d", atoi(argv[1]));
                                        
                                        if(chdir(temp)) {
                                            // se non c'e' la cartella
                                            mkdir(temp, S_IRWXU|S_IRWXG|S_IROTH|S_IRWXO);
                                            chdir(temp);
                                        }
                                        
                                       
                                        fptr = fopen("my_aggregazioni.txt", "a");
                                        if(fptr == NULL)
                                            printf("Error opening file my_aggregazioni\n");
                                        
                                        // prima riga
                                        if(startPeriod[0] == '*')
                                            fprintf(fptr, "V %c * %s 0\n", type, endPeriod);
                                        else
                                            fprintf(fptr, "V %c %s %s 0\n", type, startPeriod, endPeriod);
                                        // altre righe
                                        do{
                                            ret = recv(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                            if(ret < 0)
                                                perror("ERRORE");
                                            
                                            if(strcmp(buffer, "END_VARIAZIONI") != 0) {
                                                char current_day[11];
                                                char next_day[11];
                                                int current_variazione = 0;
                                                // salvataggio su file
                                                sscanf(buffer,"%s - %s = %d", next_day, current_day, &current_variazione);
                                                // stampa a video variazione
                                                printf("%s\n", buffer);
                                                fprintf(fptr, "- - %s %s %d\n", next_day, current_day, current_variazione);
                                            }
                                        
                                        } while(strcmp(buffer, "END_VARIAZIONI") != 0); // fin quando non ricevo questa stringa
                                        // ultima riga da inserire per far capire fine inserimento variazione
                                        fprintf(fptr, "f - - - 0\n");

                                        fclose(fptr);
                                        chdir("../");
                                        
                                        // end scrittura file

                                    } // end caso variazioni
                                    printf("\nAggregazione salvata su file\n");
                                } // end SI_DATA
                                else if(strcmp(buffer, "NO_DATA") == 0)
                                {
                                    printf("\nVicino sx NON ha dato aggregato");
                                    dato_aggregazione_vicino = 0;
                                } // end NO_DATA
                                
                                if(dato_aggregazione_vicino == 1) // cioe' trovato e stampato
                                {
                                    dato_aggregazione_vicino = 0; // resettiamo per un altro dato
                                    close(sd_tcp_vicini);
                                    sd_tcp_vicini = -1;
                                    break;
                                }

                                close(sd_tcp_vicini);
                                sd_tcp_vicini = -1;
                                // FINE invio entry al vicino di SX
                                // *******************************************

                                // ATTIVARE CONNESSIONE TCP CON VICINO DX (se i vicini sono diversi)
                                if(neighLeft != neighRight) {
                                    // ATTIVARE CONNESSIONE TCP CON VICINO DX
                                    sd_tcp_vicini = socket(AF_INET, SOCK_STREAM, 0);

                                    // assegno indirizzo nostro
                                    my_addr.sin_family = AF_INET;
                                    porta_peer = atoi(argv[1])+100;
                                    my_addr.sin_port = htons(porta_peer); // porta del PEER
                                    my_addr.sin_addr.s_addr = INADDR_ANY;

                                    memset(&neigh_addr, 0, sizeof(neigh_addr));
                                    neigh_addr.sin_family = AF_INET;
                                    neigh_addr.sin_port = htons(neighRight-100); // porta del VICINO DI DX
                                    neigh_addr.sin_addr.s_addr = INADDR_ANY;

                                    // settare la porta da input
                                    ret = bind(sd_tcp_vicini, (struct sockaddr*)&my_addr, sizeof(my_addr));

                                    printf("\nATTESA TCP RESPONSE DA VICINO DX");
                                    ret = connect(sd_tcp_vicini, (struct sockaddr*)&neigh_addr, sizeof(neigh_addr));
                                    if(ret < 0) {
                                        perror("ERRORE");
                                    }
                                    printf("\n\nTCP VICINO DX OK!\n");

                                    // inviare al VICINO di DX le proprie entry
                                    
                                    strcpy(buffer, "REQ_DATA");
                                    ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                    if(ret < 0)
                                        perror("ERRORE");

                                    // dobbiamo inviare le variabili per capire se ha STESSA aggr nel file
                                    // le variabili da inviare sono quelle che servono per la funzione find_aggr
                                    // quindi: aggr, type, startPeriod, endPeriod
                                    sprintf(buffer, "%c %c %s %s", aggr, type, startPeriod, endPeriod);
                                    ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                    if(ret < 0)
                                        perror("ERRORE");
                                    printf("\nInvio variabili per capire se ha l'aggregazione\n");
                                    
                                    ret = recv(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                    if(ret < 0)
                                        perror("ERRORE");
                                    // ricezione SI_DATA e NO_DATA
                                    if(strcmp(buffer, "SI_DATA") == 0)
                                    {
                                        printf("\nIl vicino ha inviato l'aggregazione!\n");
                                        dato_aggregazione_vicino = 1;
                                        
                                        // CASO ***** TOTALE *****
                                        if(aggr == 'T')
                                        {
                                            ret = recv(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                            if(ret < 0)
                                                perror("ERRORE");
                                            
                                            printf("\nRisultato: %d\n", atoi(buffer));

                                            // SALVATAGGIO SU FILE del TOTALE
                                            sprintf(buffer, "%s", argv[1]);
                                            
                                            if(chdir(buffer)) {
                                                // se non c'e' la cartella
                                                mkdir(buffer, S_IRWXU|S_IRWXG|S_IROTH|S_IRWXO);
                                                chdir(buffer);
                                            }
                                            
                                          
                                            fptr = fopen("my_aggregazioni.txt", "a");
                                            if(fptr == NULL)
                                                printf("Error opening file my_aggregazioni\n");
                                            else {    
                                                
                                                fprintf(fptr, "%c %c %s %s %d\n", aggr, type, startPeriod, endPeriod, atoi(buffer));
                                            
                                                fclose(fptr);
                                            }
                                            chdir("../");
                                            
                                           
                                            // FINE SALVATAGGIO SU FILE
                                        } // end caso totale
                                        // CASO ***** VARIAZIONE *****
                                        else if(aggr == 'V')
                                        {
                                            // salva su file e stampa a video
                                            char temp[30];
                                            sprintf(temp, "%d", atoi(argv[1]));
                                            
                                            if(chdir(temp)) {
                                                // se non c'e' la cartella
                                                mkdir(temp, S_IRWXU|S_IRWXG|S_IROTH|S_IRWXO);
                                                chdir(temp);
                                            }
                                            
                                            fptr = fopen("my_aggregazioni.txt", "a");
                                            if(fptr == NULL)
                                                printf("Error opening file my_aggregazioni\n");
                                            
                                            // prima riga
                                            if(startPeriod[0] == '*')
                                                fprintf(fptr, "V %c * %s 0\n", type, endPeriod);
                                            else
                                                fprintf(fptr, "V %c %s %s 0\n", type, startPeriod, endPeriod);
                                            // altre righe
                                            do{
                                                ret = recv(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                                if(ret < 0)
                                                    perror("ERRORE");
                                                
                                                if(strcmp(buffer, "END_VARIAZIONI") != 0) {
                                                    char current_day[11];
                                                    char next_day[11];
                                                    int current_variazione = 0;
                                                    // salvataggio su file
                                                    sscanf(buffer,"%s - %s = %d", next_day, current_day, &current_variazione);
                                                    // stampa a video variazione
                                                    printf("%s\n", buffer);
                                                    fprintf(fptr, "- - %s %s %d\n", next_day, current_day, current_variazione);
                                                }
                                            
                                            } while(strcmp(buffer, "END_VARIAZIONI") != 0); // fin quando non ricevo questa stringa
                                            // ultima riga da inserire per far capire fine inserimento variazione
                                            fprintf(fptr, "f - - - 0\n");

                                            fclose(fptr);
                                            chdir("../");
                                            
                                           
                                            // end scrittura file
                                        } // end caso variazioni
                                    } // end SI_DATA
                                    else if(strcmp(buffer, "NO_DATA") == 0)
                                    {
                                        printf("\nVicino dx NON ha dato aggregato");
                                        dato_aggregazione_vicino = 0;
                                    } // end NO_DATA
                                    
                                    if(dato_aggregazione_vicino == 1) // cioe' trovato e stampato
                                    {
                                        dato_aggregazione_vicino = 0; // resettiamo per un altro dato
                                        close(sd_tcp_vicini);
                                        sd_tcp_vicini = -1;
                                        break;
                                    }

                                    close(sd_tcp_vicini);
                                    sd_tcp_vicini = -1;
                                } // end se vicini sono !=
        
                                // FINE contatto vicini
                                // La ricerca nei vicini non ha prodotto risultato ---> FLOODING 
                                // FLOODING
                                printf("\n\nINIZIO FLOODING\n");
                                strcpy(buffer_conc_porta , " "); // svuoto buffer_conc_porta
                                aggregazione_flooding = aggr;

                                // Invio al vicino di DX:
                                // 1) buf_period = startPeriod endPeriod type
                                // 2) buffer_conc_porta

                                // ATTIVARE CONNESSIONE TCP CON VICINO DX
                                sd_tcp_vicini = socket(AF_INET, SOCK_STREAM, 0);

                                // assegno indirizzo nostro
                                my_addr.sin_family = AF_INET;
                                porta_peer = atoi(argv[1])+100;
                                my_addr.sin_port = htons(porta_peer); // porta del PEER
                                my_addr.sin_addr.s_addr = INADDR_ANY;

                                memset(&neigh_addr, 0, sizeof(neigh_addr)); // pulizia
                                neigh_addr.sin_family = AF_INET;
                                neigh_addr.sin_port = htons(neighRight-100); // porta del VICINO DI DX
                                neigh_addr.sin_addr.s_addr = INADDR_ANY;

                                // settare la porta da input
                                ret = bind(sd_tcp_vicini, (struct sockaddr*)&my_addr, sizeof(my_addr));

                                printf("\nATTESA TCP RESPONSE DA VICINO DX");
                                ret = connect(sd_tcp_vicini, (struct sockaddr*)&neigh_addr, sizeof(neigh_addr));
                                if(ret < 0) {
                                    perror("ERRORE");
                                }
                                printf("\n\nTCP VICINO DX OK!\n");
                                
                                strcpy(buffer, "FLOOD_FOR_ENTRIES");
                                ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                if(ret < 0)
                                    perror("ERRORE");
                                
                                printf("\nFLOOD INVIATO\n");
                                
                                // 1)
                                sprintf(buffer, "%s %s %c", startPeriod, endPeriod, type);
                                ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                if(ret < 0)
                                    perror("ERRORE");

                                // 2)
                                // copiamo la nostra porta nel buffer
                                // da questo (prima porta del buffer) capiremo quando sara' tornato da noi
                                char conc[10];
                                sprintf(conc, "%d ", atoi(argv[1]));
                                strcpy(buffer_conc_porta, conc);
                                ret = send(sd_tcp_vicini, buffer_conc_porta, MAX_CONC, 0);
                                if(ret < 0)
                                    perror("ERRORE");


                                close(sd_tcp_vicini);
                                sd_tcp_vicini = -1;
                                // FINE contatto vicino DX

                                puts("\nFLOOD INVIO RECEIVER FINITO\n");
                            }// end else (cioe' contattare i vicini e flooding)
                            // ****************************

                        } //END val_aggr == -1
                        else
                        {
                            // SE abbiamo trovato aggregazione
                            if(aggr == 'T')
                                printf("\nRisultato: %d\n", val_aggr);
                            else if(aggr == 'V')
                                printList_variazione();
                        }
                        
                    }
                    
                    // STOP
                    else if(strncmp("stop", buffer, 4) == 0) 
                    {
                        printf("Terminazione...\nInvio entry ai miei vicini\n");
                        // dobbiamo terminare col SERVER
                        strcpy(buffer, "TERMINATION");
                        ret = send(sd_tcp, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("ERRORE send termination: ");

                       
                        // CASO IN CUI NO VICINI
                        if(neighLeft == 0 && neighRight == 0)
                            goto dopo;
                    

                        // **************************
                        // INVIO ENTRIES AI VICINI

                        // ATTIVARE CONNESSIONE TCP CON VICINO SX
                        sd_tcp_vicini = socket(AF_INET, SOCK_STREAM, 0);

                        // assegno indirizzo nostro
                        my_addr.sin_family = AF_INET;
                        porta_peer = atoi(argv[1])+100; 
                        my_addr.sin_port = htons(porta_peer); // porta del PEER
                        my_addr.sin_addr.s_addr = INADDR_ANY;

                        // indirizzo del peer vicino di sx
                        memset(&neigh_addr, 0, sizeof(neigh_addr)); 
                        neigh_addr.sin_family = AF_INET;
                        neigh_addr.sin_port = htons(neighLeft-100); // porta del VICINO DI SX (listener)
                        neigh_addr.sin_addr.s_addr = INADDR_ANY;
                        
                        ret = bind(sd_tcp_vicini, (struct sockaddr*)&my_addr, sizeof(my_addr));
                       
                        printf("\nATTESA TCP RESPONSE DA VICINO SX");
                        ret = connect(sd_tcp_vicini, (struct sockaddr*)&neigh_addr, sizeof(neigh_addr));
                        if(ret < 0) {
                            perror("ERRORE");
                        }
                        printf("\n\nTCP VICINO SX OK!\n");

                        // inviare al VICINO di SX le proprie entry
                        strcpy(buffer, "START_ENTRIES");
                        ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("ERRORE");

                        temp = start;
                        while(temp != NULL)
                        {
                            sprintf(buffer, "%s %d %d %d %d", temp->buf_date, temp->porta, temp->qnt_T, temp->qnt_N, temp->lock);
                            ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("ERRORE");

                            temp = temp->next;
                        }

                        strcpy(buffer, "END_ENTRIES");
                        ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("ERRORE");

                        close(sd_tcp_vicini);
                        sd_tcp_vicini = -1;
                        // FINE invio entry al vicino di SX

                        // *************************
                        // ATTIVARE CONNESSIONE TCP CON VICINO DX
                        // se i vicini sono diversi!
                        if(neighLeft != neighRight) {
                            // ATTIVARE CONNESSIONE TCP CON VICINO SX
                            sd_tcp_vicini = socket(AF_INET, SOCK_STREAM, 0);

                            // assegno indirizzo nostro
                            my_addr.sin_family = AF_INET;
                            porta_peer = atoi(argv[1])+100;
                            my_addr.sin_port = htons(porta_peer); // porta del PEER
                            my_addr.sin_addr.s_addr = INADDR_ANY;

                            memset(&neigh_addr, 0, sizeof(neigh_addr)); 
                            neigh_addr.sin_family = AF_INET;
                            neigh_addr.sin_port = htons(neighRight-100); // porta del VICINO DI DX
                            neigh_addr.sin_addr.s_addr = INADDR_ANY;

                            // settare la porta da input
                            ret = bind(sd_tcp_vicini, (struct sockaddr*)&my_addr, sizeof(my_addr));

                            printf("\nATTESA TCP RESPONSE DA VICINO DX");
                            ret = connect(sd_tcp_vicini, (struct sockaddr*)&neigh_addr, sizeof(neigh_addr));
                            if(ret < 0) {
                                perror("ERRORE");
                            }
                            printf("\n\nTCP VICINO DX OK!\n");

                            // inviare al VICINO di SX le proprie entry
                            
                            strcpy(buffer, "START_ENTRIES");
                            ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("ERRORE");

                            temp = start;
                            while(temp != NULL)
                            {
                                sprintf(buffer, "%s %d %d %d %d", temp->buf_date, temp->porta, temp->qnt_T, temp->qnt_N, temp->lock);
                                ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                if(ret < 0)
                                    perror("ERRORE");

                                temp = temp->next;
                            }

                            strcpy(buffer, "END_ENTRIES");
                            ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("ERRORE");

                            close(sd_tcp_vicini);
                            sd_tcp_vicini = -1;
                        }
                        
                        // ****************************
                        // salvataggio su file di tutte le entry
                        
                    dopo:
                        // salvare su file le informazioni
                        sprintf(buffer, "%s", argv[1]);
                        if(chdir(buffer)) {
                            // se non c'e' la cartella
                            mkdir(buffer, S_IRWXU|S_IRWXG|S_IROTH|S_IRWXO);
                            chdir(buffer);
                        }
                        fptr = fopen("my_entries.txt", "w");
                        if(fptr == NULL)
                            printf("Error opening file my_entries\n");
                        else {    
                            temp = start;
                            while(temp != NULL)
                            {    
                                fprintf(fptr, "%s %d %d %d %d\n", temp->buf_date, temp->porta, temp->qnt_T, temp->qnt_N, temp->lock);
                                
                                temp = temp->next;
                            }
                            fclose(fptr);
                            
                        }
                        chdir("../"); 
                        // fine scrittura su file del peer
                        
                        // **********************************
                        // chiudere socket
                        
                        //close(sd_boot); 
                        close(sd_tcp);
                        sd_tcp = -1;
                        return 0;
                    }

                    // wrong
                    else 
                    {
                        printf("\nErrore inserimento comando!\n");
                    }
                
                } // end i == fileno

                // ricezione di un messaggio da un altro peer
                else if(i == sd_ricezione_peer)
                {
                    int sd_new_peer = -1;
                    int len_neig = sizeof(neigh_addr);
                    int my_port;
                    
                    sd_new_peer = accept(sd_ricezione_peer, (struct sockaddr*)&neigh_addr, (socklen_t*)&len_neig);
                    if(sd_new_peer < 0) {
                        perror("ERRORE");
                    }
                    printf("\n\naccept() effettuata con successo!\n");
                    my_port = atoi(argv[1]);

                    // controllare che TIPO DI MESSAGGIO arriva
                    ret = recv(sd_new_peer, buffer, MAXBUF, 0);
                    if(ret < 0)
                        perror("\nErrore recv sd_ricezione_peer: ");
                    
                    // START_ENTRIES
                    // cioe' un vicino ci sta inviando le sue entry. Le salviamo in memoria
                    if(strcmp(buffer, "START_ENTRIES") == 0) 
                    {
                        printf("\nEntry in arrivo\n");
                        do{ // fin quando sono diversi
        
                            ret = recv(sd_new_peer, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("\nErrore recv entries da altro peer: ");
                            
                            if(strcmp(buffer, "END_ENTRIES") != 0) {
                                temp = (struct Entry *)malloc(sizeof(struct Entry));

                                sscanf(buffer, "%s %d %d %d %d", temp->buf_date, &temp->porta, &temp->qnt_T, &temp->qnt_N, &temp->lock);
                                insert_entry(&start, temp);                          
                            }

                        }while(strcmp(buffer, "END_ENTRIES") != 0);
                        // stampa entry
                        printList(start);
                    }// FINE START_ENTRIES
                    
                    // REQ_DATA
                    // controlliamo se abbiamo l'aggregazione richiesta nel nostro file my_aggregazioni.txt
                    else if(strcmp(buffer, "REQ_DATA") == 0)
                    {
                        char aggr, type;
                        char startPeriod[11], endPeriod[11];
                        // riceviamo i dati per cercare l'aggregazione
                        ret = recv(sd_new_peer, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("\nErrore recv entries da altro peer: ");

                        sscanf(buffer, "%c %c %s %s", &aggr, &type, startPeriod, endPeriod);
                        free_list_variazione();
                        int risultato = find_aggr(aggr, type, startPeriod, endPeriod, atoi(argv[1])); // con la nostra porta
                            
                        if(risultato == -1)
                        {    
                            // dato aggregazione NON trovato
                            strcpy(buffer, "NO_DATA");
                            puts("\nNo dato aggregazione corrispondente\n");
                            ret = send(sd_new_peer, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("ERRORE");
                        }
                        else {
                            // dato aggregazione TROVATO
                            strcpy(buffer, "SI_DATA");
                            puts("\nTrovato dato aggregazione corrispondente\n");
                            ret = send(sd_new_peer, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("ERRORE");
                            // TOTALE
                            if(aggr == 'T'){
                                // dobbiamo inviare il risultato
                                sprintf(buffer, "%d", risultato);
                                printf("\nRisultato inviato: %d\n", risultato);
                                ret = send(sd_new_peer, buffer, MAXBUF, MSG_WAITALL);
                                if(ret < 0)
                                    perror("ERRORE");
                                
                            }
                            // VARIAZIONE
                            else if(aggr == 'V') {
                            // nella lista lista_var ho le variazioni da spedire
                                struct List_variazione *t = lista_var;
                                while(t){
                                    sprintf(buffer, "%s",t->buffer);
                                    ret = send(sd_new_peer, buffer, MAXBUF, MSG_WAITALL);
                                    if(ret < 0)
                                        perror("ERRORE");
                                    t = t->next;                                    
                                }

                                strcpy(buffer, "END_VARIAZIONI");
                                ret = send(sd_new_peer, buffer, MAXBUF, MSG_WAITALL);
                                if(ret < 0)
                                    perror("ERRORE");
                                puts("\nVariazioni inviate\n");
                            }

                        }

                    } // FINE REQ_DATA
                    // FLOOD_FOR_ENTRIES
                    // flooding, concateniamo il nostro numero di porta nel buffer
                    // se abbiamo entry nel periodo specificato
                    else if(strcmp(buffer, "FLOOD_FOR_ENTRIES") == 0)
                    {
                        printf("\nFLOOD_FOR_ENTRIES ricevuto\n");
                        char iniP[11], endP[11], buffer_conc_current[1000];
                        int ismyport; // utilizzato per controllare se e' finito il giro
                        char t;
                        // Arriva adesso una coppia di messaggi:
                        // 1) startP endP tipo
                        // 2) tutte le porte dei peer che hanno entries nel periodo
                        ret = recv(sd_new_peer, buffer, MAXBUF, 0);
                        if(ret < 0)
                            perror("\nErrore recv: ");
                        // 1)
                        sscanf(buffer, "%s %s %c", iniP, endP, &t);
                        ret = recv(sd_new_peer, buffer_conc_current, MAX_CONC, 0);
                        if(ret < 0)
                            perror("\nErrore recv: ");
                        // 2)
                        strcpy(buffer_conc_porta, buffer_conc_current); 
                        // copiare su ismyport la prima porta per vedere se siamo il requester
                        sscanf(buffer_conc_porta, "%d %*s", &ismyport);
                        
                        // per prima cosa vediamo se la prima porta del buffer corrisponde
                        // alla nostra porta: giro concluso
                        
                        if(ismyport != my_port) {
                            
                            // NON siamo il requester
                            // se ha entry nel periodo, concateniamo la nostra porta
                            int num = conta_entries(start, t, iniP, endP);
                            if(num > 0) {
                                // concateniamo la sua porta al buffer
                                char conc[10];
                                sprintf(conc, "%d ", atoi(argv[1]));
                                strcat(buffer_conc_porta, conc);
                            }
                            printf("\nPassiamo i buffer al vicino di dx...\n");
                            // invece se non abbiamo entries, spediamo buffer_conc_porta cosi' come arrivato
                            // spediamo sempre al vicino di destra

                            // ATTIVARE CONNESSIONE TCP CON VICINO DX
                            sd_tcp_vicini = socket(AF_INET, SOCK_STREAM, 0);

                            // assegno indirizzo nostro
                            my_addr.sin_family = AF_INET;
                            porta_peer = atoi(argv[1])+100;
                            my_addr.sin_port = htons(porta_peer); // porta del PEER
                            my_addr.sin_addr.s_addr = INADDR_ANY;

                            memset(&neigh_addr, 0, sizeof(neigh_addr)); 
                            neigh_addr.sin_family = AF_INET;
                            neigh_addr.sin_port = htons(neighRight-100); // porta del VICINO DI DX
                            neigh_addr.sin_addr.s_addr = INADDR_ANY;

                            // settare la porta da input
                            ret = bind(sd_tcp_vicini, (struct sockaddr*)&my_addr, sizeof(my_addr));

                            
                            ret = connect(sd_tcp_vicini, (struct sockaddr*)&neigh_addr, sizeof(neigh_addr));
                            if(ret < 0) {
                                perror("ERRORE");
                            }
                            printf("\n\nconnect() effettuata con successo!\n");

                            // passiamo i due buffer
                            strcpy(buffer, "FLOOD_FOR_ENTRIES");
                            ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("ERRORE");
                            // 1)
                            sprintf(buffer, "%s %s %c", iniP, endP, t);
                            ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("ERRORE");
                            // 2)
                            ret = send(sd_tcp_vicini, buffer_conc_porta, MAX_CONC, 0);
                            if(ret < 0)
                                perror("ERRORE");


                            close(sd_tcp_vicini);
                            sd_tcp_vicini = -1;
                            // FINE contatto vicino DX

                        }
                        else {
                            // siamo noi il REQUESTER
                            printf("\nCICLO FLOODING TERMINATO\n");
                            char *token;
                            token = strtok(buffer_conc_porta, " ");
                            // CONTATTARE tutti i peer che corrispondono alle porte che sono presenti
                            // nel buffer_conc_porta e ci facciamo inviare tutte le loro entry
                            // e le aggiungiamo nella nostra lista
                            while(token != NULL) 
                            {
                                // rifacciamo strtok() adesso perche' la prima porta la ignoriamo
                                token = strtok(NULL, " ");
                                if(token != NULL) {
                                    int peer_target = atoi(token);
                                    // Contattare il peer peer_target

                                    // ATTIVARE CONNESSIONE TCP CON peer_target
                                    printf("\nIn arrivo entry dal peer %d\n", peer_target);
                                    sd_tcp_vicini = socket(AF_INET, SOCK_STREAM, 0);

                                    // assegno indirizzo nostro
                                    my_addr.sin_family = AF_INET;
                                    porta_peer = atoi(argv[1])+100;
                                    my_addr.sin_port = htons(porta_peer); // porta del PEER
                                    my_addr.sin_addr.s_addr = INADDR_ANY;

                                    memset(&neigh_addr, 0, sizeof(neigh_addr)); 
                                    neigh_addr.sin_family = AF_INET;
                                    neigh_addr.sin_port = htons(peer_target-100); // porta del peer_target
                                    neigh_addr.sin_addr.s_addr = INADDR_ANY;

                                    // settare la porta da input
                                    ret = bind(sd_tcp_vicini, (struct sockaddr*)&my_addr, sizeof(my_addr));

                                    printf("\nATTESA TCP RESPONSE DA peer_target");
                                    ret = connect(sd_tcp_vicini, (struct sockaddr*)&neigh_addr, sizeof(neigh_addr));
                                    if(ret < 0) {
                                        perror("ERRORE");
                                    }
                                    printf("\nTCP peer_target OK!\n");

                                    // Invio messaggio GIVE_ENTRIES
                                    strcpy(buffer, "GIVE_ENTRIES");
                                    ret = send(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                    if(ret < 0)
                                        perror("ERRORE");
                                    
                                    // adesso dobbiamo ricevere le entries
                                    do{ // fin quando sono diversi

                                        ret = recv(sd_tcp_vicini, buffer, MAXBUF, MSG_WAITALL);
                                        if(ret < 0)
                                            perror("\nErrore recv entries da altro peer: ");
                                        

                                        if(strcmp(buffer, "END_ENTRIES") != 0){
                                            temp = (struct Entry *)malloc(sizeof(struct Entry));

                                            sscanf(buffer, "%s %d %d %d %d", temp->buf_date, &temp->porta, &temp->qnt_T, &temp->qnt_N, &temp->lock);
                                            insert_entry(&start, temp);                          
                                        }

                                    } while(strcmp(buffer, "END_ENTRIES") != 0);

                                    printf("\nEntry dal peer %d arrivate\n", peer_target);
                                    close(sd_tcp_vicini);
                                    sd_tcp_vicini = -1;
                                    // FINE contatto peer_target

                                }// end if token NOT NULL

                            } // end while token NOT NULL

                            // adesso si calcola l'aggregazione che si doveva calcolare
                            // codice uguale a sopra: dopo che verifica che il num di entries e' = al num di entries inviate dal server
                            // CALCOLO TOTALE
                            if(aggregazione_flooding == 'T') {
                                int risultato = calcolo_totale(start, t, iniP, endP);
                                printf("\nTOTALE: %d", risultato);
                                // SALVATAGGIO SU FILE del TOTALE
                                sprintf(buffer, "%s", argv[1]);
                                
                                if(chdir(buffer)) {
                                    // se non c'e' la cartella
                                    mkdir(buffer, S_IRWXU|S_IRWXG|S_IROTH|S_IRWXO);
                                    chdir(buffer);
                                }
                                
                                
                                fptr = fopen("my_aggregazioni.txt", "a");
                                if(fptr == NULL)
                                    printf("Error opening file my_aggregazioni\n");
                                else {    
                                    
                                    fprintf(fptr, "%c %c %s %s %d\n", aggregazione_flooding, t, iniP, endP, risultato);
                                
                                    fclose(fptr);
                                }
                                chdir("../");
                                
                                
                                // FINE SALVATAGGIO SU FILE
                            }
                            // CALCOLO VARIAZIONE
                            else if(aggregazione_flooding == 'V') {
                                printf("\nSto calcolando la variazione");
                                calcolo_variazione(start, t, iniP, endP, atoi(argv[1]));                                
                                
                            }


                        } // end siamo noi REQUESTER aggregazione

                    }
                    // end FLOOD_FOR_ENTRIES

                    // GIVE_ENTRIES
                    // invio entries al peer, usato nel flooding quando il requester ce le richiede
                    else if(strcmp(buffer, "GIVE_ENTRIES") == 0)
                    {
                        temp = start;
                        while(temp != NULL)
                        {
                            sprintf(buffer, "%s %d %d %d %d", temp->buf_date, temp->porta, temp->qnt_T, temp->qnt_N, temp->lock);
                            ret = send(sd_new_peer, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("ERRORE");

                            temp = temp->next;
                        }

                        strcpy(buffer, "END_ENTRIES");
                        ret = send(sd_new_peer, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("ERRORE");
                    }
                    
                    close(sd_new_peer);
                    sd_new_peer = -1;

                } // end i == sd_ricezione_peer
            } // end if ISSET


        } // end for <= fdmax

    } // end while(1)
    
}
