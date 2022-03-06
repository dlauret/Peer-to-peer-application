#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>

#include <inttypes.h>

#define MAXBUF 256

// struttura che rappresenta un peer attivo
// il ring sarà ordinato per porta
struct Elem_ring
{
    unsigned int porta;
    int s_tcp; // da usare per la comunicazione con il peer
    struct Elem_ring *next;    
};

// viene utilizzata per memorizzare le entry che ci inviano i peer
// quando si fa la esc
struct Entry {
    char buf_date[11]; // AAAA/MM/GG
    int porta; // peer
    int qnt_T; // quantita' tamponi
    int qnt_N; // quantita' nuovi casi
    int lock; // per vedere se bloccare o no il registro
    struct Entry *next; // punta a quella di un giorno successivo
};

// inserimento di una entry: inserimento IN TESTA
void insert_entry(struct Entry** head_ref, struct Entry* new_node)
{
    // dobbiamo controllare se c'e' una entry con stessa data e stessa porta
    // in questo caso lo IGNORIAMO SE la quantita' della entry <=
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

// funzione per stampare nodi lista
void printList_entry(struct Entry *start)
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

// funzione che controlla se e' presente un nodo con data e tipo passata
// cosi' di aumentare solo la quantita'
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

// funzione per inserire in modo ordinato
// serve per inserimento di un nuovo peer nell'anello
void sortedInsert(struct Elem_ring ** head_ref, struct Elem_ring* new_node)
{
    struct Elem_ring* current = *head_ref;

    // caso LISTA VUOTA
    if(current == NULL) {
        new_node->next = new_node;
        *head_ref = new_node;
    }
    // caso nodo da inserire PRIMA del nodo head
    else if(current->porta >= new_node->porta) {
        // dobbiamo cambiare il next dell'ultimo nodo
        while(current->next != *head_ref)
            current = current->next;
        current->next = new_node;
        new_node->next = *head_ref;
        *head_ref = new_node;
    }
    // caso nodo in qualsiasi altro posto nel ring
    else {
        while (current->next != *head_ref && current->next->porta < new_node->porta)
            current = current->next;

        new_node->next = current->next;
        current->next = new_node;
        
    }
}

// funzione per eliminare elemento ring
void deleteNode(struct Elem_ring **head_ref, unsigned int k)
{
    
    struct Elem_ring* temp = *head_ref, *prev;
    // se il nodo da cancellare e' la testa
    if(temp != NULL && temp->porta == k) 
    {
        if(temp->next == temp) { // se unico elemento 
            free(*head_ref);
            *head_ref = NULL;
            return;
        }
        struct Elem_ring* current = *head_ref;
        while(current->next != *head_ref)
            current = current->next;

        *head_ref = temp->next; // la testa punta al 2ndo nodo
        //adesso anche l'ultimo nodo deve puntare alla nuova testa (ring)
        current->next = *head_ref;

        free(temp);
        return;
    }

    while (temp->next != *head_ref && temp->porta != k) 
    { 
        prev = temp; 
        temp = temp->next; 
    }

    if(temp->porta == k){ // trovato elemento
        // se eliminiamo l'ultimo e quello precedente era la testa,
        // il prossimo della testa deve puntare null
        if(prev->next == *head_ref && temp->next == *head_ref){
            prev->next = NULL;
        }
        else
            prev->next = temp->next;
        
    }
    else
        return;
    
    free(temp);  
    
}

// stampa a video lista ring
void printList(struct Elem_ring *start)
{
    struct Elem_ring *temp;
    
    if(start == NULL) // se non ci sono peer
    {
        printf("\nNon sono presenti peer nella rete!\n");
        
    }
    else
    {
        printf("\n");
        temp = start;
        do {
            printf("%d \t", temp->porta);
            temp = temp->next;
        } while(temp != start);
        printf("\n");
    }

}

int count_peer = 0;
// var globali usati per assegnare momentaneamente i vicini
int vicinoL, vicinoR;
// funzione che trova i vicini (le porte dei vicini) passando come parametro il numero di porta
// saranno il precedente e il successivo dell'anello
void findNeighbors(struct Elem_ring *start, unsigned int p) {
    struct Elem_ring *prev, *temp = start;

    if(count_peer == 1) {
        vicinoL = vicinoR = 0;
    }
    else {
        while (temp->porta != p && temp->next != start) 
        { 
            prev = temp;
            temp = temp->next;
        }

        vicinoR = temp->next->porta; // vicino right va bene

        if(temp == start) // quindi il primo
        {
            // per il vicinoL bisogna prendere l'ultimo peer
            while (temp->next != start)
                temp = temp->next;

            vicinoL = temp->porta; // cioè l'ultimo del ring
        }
        else { // se non e' il primo il precedente va bene
            vicinoL = prev->porta;
        }        

    }

}

// funzione per assegnare il nuovo socket tcp al nodo corrispondente (cioe' porta p) 
void assign_sd(struct Elem_ring *start, unsigned int p, int new_sd)
{
    struct Elem_ring *ini = start;
    while(start->porta != p && start->next != ini) 
        start = start->next;
    
    start->s_tcp = new_sd;
}

// funzione che dato come parametro una porta restituisce il socket tcp associato
int getSocket(struct Elem_ring *start, unsigned int p) {
    struct Elem_ring *ini = start;

    while(start->porta != p && start->next != ini) 
        start = start->next;
    
    return start->s_tcp;
}

// funzione che dato come parametro un socket restituisce la porta associata
unsigned int getPort(struct Elem_ring *start, int s) {
    struct Elem_ring *ini = start;

    while(start->s_tcp != s && start->next != ini) 
        start = start->next;
    
    return start->porta;
}

// funzione che ritorna il numero di entries in un determinato periodo (del tipo specificato)
// usata per la get del peer
// Inoltre, se tra queste troviamo una entry con lock = 0 nel periodo ricevuto, restituiamo -1
int conta_entries(struct Entry *start, char type, char startPeriod[11], char endPeriod[11])
{
    struct Entry *temp = start;
    int counter = 0;
    // casi:
    // 1) startPeriod = * --> controllo solo <= endPeriod
    // 2) startPeriod e endPeriod sono due date normali
    // In tutti devo controllare se la entyi ha lock = 1
    // se ha lock = 0, allora ritorniamo -1

    // 1)
    if(startPeriod[0] == '*')
    {
        while (temp != NULL)
        {
            
            // se e' locked e data temp e' minore(=) di endPeriod
            if(strcmp(temp->buf_date, endPeriod) <= 0)
            {
                // se e' aperto restituiamo -1
                if(temp->lock == 0)
                    return -1;

                // aggiorno contatore
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
                // se e' aperto restituiamo -1
                if(temp->lock == 0)
                    return -1;

                // aggiorniamo counter    
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



int main(int argc, char* argv[]) {
    int ret, new_sd, nbytes;
    char buffer[MAXBUF];
    struct sockaddr_in ds_addr, peer_addr; // 2 struct

    // variabili per la struct Elem_ring
    struct Elem_ring *start = NULL;
    struct Elem_ring *temp = NULL;
    // variabili per la struct Entry
    struct Entry *start_entry = NULL;
    struct Entry *temp_entry = NULL;


    // 'select'
    fd_set master, read_fds;
    int fdmax; // num max di descrittori

    FD_ZERO(&master);
    FD_ZERO(&read_fds); 
    
    int i;
    int len = sizeof(peer_addr);
    int bool_close = 0; // usato nella close (ESC)

    // puntatore a file
    FILE *fptr;


    // CARICARE IN MEMORIA LE ENTRY SE SONO PRESENTI NEL FILE final_entries.txt 
    fptr = fopen("final_entries.txt", "r");
    if(fptr == NULL)
        printf("File da cui caricare entry non presente\n");
    else {
        // se file e' presente
        char riga[100];
        char *retF;

        while(1){
            retF = fgets(riga, 100, fptr);
            if(retF == NULL) // cioe' EOF
                break;
            
            temp_entry = (struct Entry *)malloc(sizeof(struct Entry));     
            sscanf(riga,"%s %d %d %d %d", temp_entry->buf_date, &temp_entry->porta, &temp_entry->qnt_T, &temp_entry->qnt_N, &temp_entry->lock);                 
            insert_entry(&start_entry, temp_entry);     
        }
        
        fclose(fptr);
        printList_entry(start_entry);
    }
    // FINE CARICAMENTO ENTRY


 // ***********TCP***********************
    int tcp_socket;
    tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    // creazione indirizzo
    memset(&ds_addr, 0, sizeof(ds_addr)); // pulizia
    ds_addr.sin_family = AF_INET;
    unsigned int porta_ds = atoi(argv[1]);
    ds_addr.sin_port = htons(porta_ds);
    ds_addr.sin_addr.s_addr = INADDR_ANY;

    ret = bind(tcp_socket, (struct sockaddr*)&ds_addr, sizeof(ds_addr));
    ret = listen(tcp_socket, 50);

    // *************************************


    // ***********UDP***********************
    // creazione socket di BOOT --> UDP
    int boot_socket;
    boot_socket = socket(AF_INET, SOCK_DGRAM, 0);
    
    ret = bind(boot_socket, (struct sockaddr*)&ds_addr, sizeof(ds_addr));
    if(ret < 0)
        perror("\nERRORE BIND UDP\n");
    // ************************************



    FD_SET(boot_socket, &master);
    FD_SET(tcp_socket , &master);
    FD_SET(fileno(stdin), &master);

    if(boot_socket > tcp_socket)
        fdmax = boot_socket;
    else
        fdmax = tcp_socket;


    // variabili per gestire la data e l'ora
    time_t rawtime;
    struct tm *info_time;
    time(&rawtime);
    info_time = localtime(&rawtime);
    
    while(1) {
        read_fds = master;

        // controllare se sono le 18 per chiudere i registri odierni (stessa operazione che e' svolta dai peer)
        time(&rawtime);
        info_time = localtime(&rawtime);

        if(info_time->tm_hour >= 18)
        {
            struct Entry *aux = start_entry;
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


        //MENU'
        printf("\n**************Discovery Server**************");
        printf("\n\nDigita comando:\n\n");
        printf("1) help\n2) showpeers\n3) showneighbor <peer>\n4) esc\n");

        // SELECT      
        int n  = select(fdmax + 1, &read_fds, NULL, NULL, NULL);
        if(n<0)
            perror("ERRORE SELECT: ");

        for(i = 0; i <= fdmax; i++)
        {
            if(FD_ISSET(i, &read_fds))
            {
                // fase di BOOT del peer
                if(i == boot_socket) {
                    // il socket e' quello di boot, dobbiamo registrare il peer
                    // utilizzo la porta
                    in_port_t peer_port;
                    count_peer = count_peer + 1; // incrementiamo il numero di peer

                    // buffer <-- numero di porta del peer
                    nbytes = recvfrom(boot_socket, buffer, MAXBUF, MSG_WAITALL, (struct sockaddr*)&peer_addr, (socklen_t*)&len);
                    if(nbytes < 0) /*error handing*/
                        perror("ERROR in recvfrom() during peer BOOT\n");
                    
                    peer_port = atoi(buffer);

                    // notifichiamo l'avvenuta registrazione tramite un ACK (come da specifica)
                    strcpy(buffer, "ACK");            
                    nbytes = sendto(boot_socket, buffer, MAXBUF, 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
                    if(nbytes < 0) /*error handing*/
                        perror("ERROR in sento() during peer BOOT\n");
                        
         
                    // INSERIMENTO ordinato nella lista (ring) del peer
                    temp = (struct Elem_ring *)malloc(sizeof(struct Elem_ring));
                    temp->porta = peer_port;
                    //temp->s_tcp = -1;

                    sortedInsert(&start, temp);
                    // FINE inserimento

                    
                    // Comunicare al peer i 2 neighbor (sx e dx)
                    // var usate: vicinoL, vicinoR
                    findNeighbors(start, peer_port);
                    // SX
                    sprintf(buffer, "%u",vicinoL);
                    nbytes = sendto(boot_socket, buffer, MAXBUF, 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
                    if(nbytes < 0) /*error handing*/
                        perror("ERROR in sento() invio vicino sx\n");
                        
                    // DX
                    sprintf(buffer, "%u",vicinoR);
                    nbytes = sendto(boot_socket, buffer, MAXBUF, 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
                    if(nbytes < 0) /*error handing*/
                        perror("ERROR in sento() invio vicino dx\n");
                        

                    // comunicare ad eventuali peer i suoi nuovi neighbor
                    // i peer da avvisare saranno i vicini del peer che abbiamo inserito
                    // hai già i loro socket perche' hanno fatto prima il boot
                    // prendiamo i socket così da poter comunicare con i rispettivi peer
                    int sock_sx, sock_dx;

                    // variabili usate per ricordare i vicini del peer
                    // dato che VicinoL e vicinoR verranno sovrascritti
                    int old_vicinoL = vicinoL, old_vicinoR = vicinoR;

                    if(count_peer == 1)
                        break;

                    sock_sx = getSocket(start, vicinoL);
                    sock_dx = getSocket(start, vicinoR);
                    
                    // COMUNICAZIONE CON VICINO L
                    if(sock_sx != -1) {
                        findNeighbors(start, old_vicinoL);
                        // in vicinoL e vicinoR ci sono i nuovi vicini di old_vicinoL

                        // invio messaggio UPDATE VICINI così capisce che deve ricevere 2 mex
                        strcpy(buffer, "UPDATE_NEIGHBORS");
                        ret = send(sock_sx, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("\nErrore send 1 vicinoL: ");
                        // invio a old_vicinoL i nuovi vicini
                        sprintf(buffer, "%u",vicinoL);
                        ret = send(sock_sx, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("\nErrore send 1 vicinoL: ");
                        
                        sprintf(buffer, "%u",vicinoR);
                        ret = send(sock_sx, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("\nErrore send 1 vicinoL: ");
                    }

                    if(sock_sx == sock_dx) // abbiamo gia' comunicato
                            break;

                    // COMUNICAZIONE CON VICINO R
                    if(sock_dx != -1) {
                        findNeighbors(start, old_vicinoR);
                        // in vicinoL e vicinoR ci sono i nuovi vicini di old_vicinoR

                        // invio messaggio UPDATE VICINI così capisce che deve ricevere 2 mex
                        strcpy(buffer, "UPDATE_NEIGHBORS");
                        ret = send(sock_dx, buffer, MAXBUF, MSG_WAITALL);
                        //ret = send(sock_dx, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("\nErrore send 1 vicinoR: ");
                        // invio a old_vicinoL i nuovi vicini
                        sprintf(buffer, "%u",vicinoL);
                        ret = send(sock_dx, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("\nErrore send 1 vicinoR: ");
                        
                        sprintf(buffer, "%u",vicinoR);
                        ret = send(sock_dx, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("\nErrore send 1 vicinoR: ");
                    }

                }
                // tcp col peer
                else if(i == tcp_socket) {
                    // **************************************************
                    // peer si connette in TCP nella sua fase di boot
                    // ACCETTARE connessione TCP col peer sul socket tcp
                    new_sd = accept(tcp_socket, (struct sockaddr*)&peer_addr, (socklen_t*)&len);
                    if(new_sd < 0)
                        perror("\n\nERRORE TCP\n\n");
                    
                    // ricezione porta
                    ret = recv(new_sd, buffer, MAXBUF, MSG_WAITALL);
                    printf("\nPorta strana: %u\n\n", ntohs(peer_addr.sin_port));

                    // devo trovare la porta per salvare nel nodo il new_sd, cioe' il nuovo socket per la comunicazione TCP
                    assign_sd(start, atoi(buffer), new_sd);
                    FD_SET(new_sd, &master);
                    if(new_sd > fdmax)
                        fdmax = new_sd;
                   
                    // FINE ACCETTAZIONE

                    // *************************************
                    // INVIO ENTRIES SE E' IL PRIMO PEER
                    // esse saranno nella struttura dati. Le abbiamo caricate all'inizio.
                    // controllare anche se la struttura dati e' NON vuota, cioe' se c'era
                    // il file da cui caricare
                    if(count_peer == 1 && start_entry != NULL)
                    {
                        // FIRST_ENTRIES: indica che e' il primo peer
                        strcpy(buffer, "FIRST_ENTRIES");
                        ret = send(new_sd, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("ERRORE send FIRST_ENTRIES: ");

                        strcpy(buffer, "START_ENTRIES");
                        ret = send(new_sd, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("ERRORE");

                        temp_entry = start_entry;
                        while(temp_entry != NULL)
                        {
                            sprintf(buffer, "%s %d %d %d %d", temp_entry->buf_date, temp_entry->porta, temp_entry->qnt_T, temp_entry->qnt_N, temp_entry->lock);
                            ret = send(new_sd, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("ERRORE");

                            temp_entry = temp_entry->next;
                        }

                        strcpy(buffer, "END_ENTRIES");
                        ret = send(new_sd, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("ERRORE");
                        
                    }
                    // FINE INVIO ENTRIES

                } // end i == tcp_socket
                // stdin
                else if(i == fileno(stdin)) {

                    if(fgets(buffer, MAXBUF, stdin) == NULL)
                        perror("\nERRORE fgets");
                    
                    // help
                    if(strncmp("help", buffer, 4) == 0)
                    {
                        printf("\nSpiegazione comandi:\n");
                        printf("\nshowpeers: visualizza tutti i peer che fanno parte della rete\n");
                        printf("showneighbor peer: visualizza i vicini del peer inserito, se non si inserisce \n");

                    } // end help
                    // SHOWPEERS
                    else if(strncmp("showpeers", buffer, 9) == 0)
                    {
                        printf("\nELENCO LISTA PEER:\n");
                        printList(start);
                    } // end SHOWPEERS
                    // SHOWNEIGHBOR <PEER>
                    else if(strncmp("showneighbor", buffer, 12) == 0)
                    {
                        if(start == NULL)
                        {
                            printf("\nNon sono presenti peer nella rete!\n");
                            break;
                        }

                        unsigned int peer = 0;
                        sscanf(buffer, "%*s %u", &peer);
                        
                        if(peer == 0)
                        {
                            temp = start;
                            while(temp->next != start)
                            {
                                findNeighbors(start, temp->porta);
                                printf("Vicini di %u --> %u\t%u\n", temp->porta, vicinoL, vicinoR);
                                temp = temp->next;
                            }
                        }
                        else {
                            findNeighbors(start, peer);
                            printf("%u\t%u\n", vicinoL, vicinoR);
                        }
                    }
                    // ESC
                    else if(strncmp("esc", buffer, 3) == 0) 
                    {
                        // mandare END_PEER ad ogni peer
                        bool_close = 1; // per dire che nell'if del response close successivo deve terminare il DS
                        if(count_peer == 0) // se non ci sono peer
                            goto epilogue;
                        
                        temp = start;
                        do // tutta la lista (si ferma all'ultimo)
                        {
                            strcpy(buffer, "CLOSE");
                            
                            ret = send(getSocket(start, temp->porta), buffer, MAXBUF, MSG_WAITALL); // manda tramite il suo socket
                            if(ret < 0)
                                perror("\nERRORE SEND CLOSE:");
                            
                            temp = temp->next;
                        }while(temp != start);

                        // settare ad 1 tutti i lock delle entry
                        temp_entry = start_entry;
                        while(temp_entry != NULL)
                        {
                            temp_entry->lock = 1;
                            temp_entry = temp_entry->next;
                        }
                        // fine lock = 1 delle entries

                        
                    } // end CLOSE (esc)
                        
                } // end i == fileno(stdin)
                
                // tutti i descrittori creati dalla comunicazione tcp
                // per comunicare con i peer
                else {
                    // riceviamo messaggio e controlliamo il tipo
                    recv(i, buffer, MAXBUF, MSG_WAITALL);
                    if(ret < 0)
                        perror("\nErrore recv: ");
                    
                    // CASI VARI DI COMUNICAZIONE DA PARTE DI UN PEER
                    if(strcmp(buffer, "TERMINATION") == 0) {
                        // TERMINAZIONE PEER 
                        // 1) aggiornare vicini
                        // 2) comunicare i nuovi vicini ai vicini
                        // 3) chiusura porta comunicazione tcp col peer

                        
                        count_peer = count_peer - 1;
                        // prendiamo la porta associata al socket per capire il peer che deve essere eliminato
                        unsigned int curr_port = getPort(start, i);
                        findNeighbors(start, curr_port); 
                        // in vicinoL e vicinoR ci sono i nuovi vicini di curr_port
                        // li salviamo in delle variabili
                        int old_vicinoL = vicinoL, old_vicinoR = vicinoR;

                        // adesso eliminiamo dalla lista il nodo (peer) che sta terminando
                        deleteNode(&start, curr_port);
                        printList(start);
                        
                        if(count_peer == 0){
                            goto end_termination;
                        }
                        
                        // adesso, come detto prima, in old_vicinoL e old_vicinoR
                        // ci sono i nuovi vicini di curr_port
                        int sock_sx = getSocket(start, vicinoL);
                        int sock_dx = getSocket(start, vicinoR);
                    
                        // COMUNICAZIONE CON VICINO L
                        printf("\n\nCOMUNICAZIONE VICINI\n");
                        if(sock_sx != -1) {
                            if(count_peer == 1) // il penultimo sta finendo
                            {
                                // quindi settiamo i vicini dell'ultimo a 0
                                vicinoL = vicinoR = 0;
                            }
                            else // non e' il penultimo
                                findNeighbors(start, old_vicinoL);
                            // in vicinoL e vicinoR ci sono i nuovi vicini di old_vicinoL

                            // invio messaggio UPDATE VICINI così capisce che deve ricevere 2 mex
                            strcpy(buffer, "UPDATE_NEIGHBORS");
                            ret = send(sock_sx, buffer, MAXBUF, MSG_WAITALL);
                            //ret = send(sock_sx, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("\nErrore send 1 vicinoL: ");
                            // invio a old_vicinoL i nuovi vicini
                            sprintf(buffer, "%u",vicinoL);
                            ret = send(sock_sx, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("\nErrore send 1 vicinoL: ");
                            
                            sprintf(buffer, "%u",vicinoR);
                            ret = send(sock_sx, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("\nErrore send 1 vicinoL: ");
                        }

                        if(sock_sx == sock_dx) // abbiamo gia' comunicato
                                goto end_termination;

                        // COMUNICAZIONE CON VICINO R
                        if(sock_dx != -1) {
                            if(count_peer == 1) // il penultimo sta finendo
                            {
                                vicinoL = vicinoR = 0;
                            }
                            else // non e' il penultimo
                                findNeighbors(start, old_vicinoR);
                            // in vicinoL e vicinoR ci sono i nuovi vicini di old_vicinoR

                            // invio messaggio UPDATE VICINI così capisce che deve ricevere 2 mex
                            strcpy(buffer, "UPDATE_NEIGHBORS");
                            ret = send(sock_dx, buffer, MAXBUF, MSG_WAITALL);
                            //ret = send(sock_dx, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("\nErrore send 1 vicinoL: ");
                            // invio a old_vicinoL i nuovi vicini
                            sprintf(buffer, "%u",vicinoL);
                            ret = send(sock_dx, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("\nErrore send 1 vicinoL: ");
                            
                            sprintf(buffer, "%u",vicinoR);
                            ret = send(sock_dx, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("\nErrore send 1 vicinoR: ");
                        }

                        end_termination:
                        close(i);
                        FD_CLR(i, &master);

                    } // end if buf == TERMINATION
                    // SEND_ENTRY
                    else if(strcmp(buffer, "SEND_ENTRY") == 0) {
                        // durante la add, il peer invia la entry

                        // variabili per l'inserimento
                        char buf_time[11];
                        char type;
                        int quantity;
                        int p;

                        ret = recv(i, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("\nErrore recv i: ");

                        sscanf(buffer, "%s %c %d %d", buf_time, &type, &quantity, &p);
                        // controlliamo se presente  una entry cosi' da aggiornare la quantita'
                        ret = if_present_assign(start_entry, buf_time, type, quantity, p);
                        //se non e' presente -> ret = 0
                        if(ret == 0) // cioe' non e' presente
                        {
                            // se e' il primo del giorno
                            // dobbiamo assegnare la quantita' al tipo corretto
                            temp_entry = (struct Entry *)malloc(sizeof(struct Entry));
                            strcpy(temp_entry->buf_date, buf_time);

                            if(type == 'T')
                            {
                                temp_entry->qnt_N = 0;
                                temp_entry->qnt_T = quantity;
                            }
                            else if(type == 'N')
                            {
                                temp_entry->qnt_N = quantity;
                                temp_entry->qnt_T = 0;
                            }
                            
                            temp_entry->porta = p;
                            insert_entry(&start_entry, temp_entry);
                        }
                        printList_entry(start_entry);
                
                    } // end SEND_ENTRY
                    // HOW_MANY_ENTRIES
                    else if(strcmp(buffer, "HOW_MANY_ENTRIES") == 0) {
                        // conta le entries presenti nel periodo selezionato,
                        // se inoltre ce n'è qualcuna aperta, invia -1.
                        // se endP = '*' invia la data maggiore con lock = 1
                        char tipo;
                        char startP[11], endP[11];
                        
                        // DOBBIAMO RICEVERE: tipo, startP, endP
                        ret = recv(i, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("\nErrore recv i: ");
                        
                        sscanf(buffer, "%c %s %s", &tipo, startP, endP);

                        // SE endP = '*'
                        // ALLORA inviamo al peer la data maggiore con lock = 1;
                        if(strcmp(endP, "*") == 0) {
                            char data_massima[11];
                            struct Entry *t = start_entry;
                            strcpy(data_massima, "1000/01/01"); // data di partenza per confronto
                            if(!start_entry) // se non ci sono entry
                                strcpy(data_massima, "2100/01/01");

                            while(t != NULL)
                            {
                                // se t->data maggiore e entry non e' bloccata
                                if((strcmp(t->buf_date, data_massima) > 0) 
                                    && (t->lock == 1))
                                    sprintf(data_massima, "%s", t->buf_date);
                                t = t->next;
                            }

                            // invio al peer
                            strcpy(endP, data_massima); // sovrascriviamo ad endP
                            strcpy(buffer, data_massima);
                            ret = send(i, buffer, MAXBUF, MSG_WAITALL);
                            if(ret < 0)
                                perror("\nErrore send i: ");
                        }

                        // calcolo numero entries
                        int how_many = conta_entries(start_entry, tipo, startP, endP);
                        
                        sprintf(buffer,"%d", how_many);
                        ret = send(i, buffer, MAXBUF, MSG_WAITALL);
                        if(ret < 0)
                            perror("\nErrore send i: ");
                        
                        puts("\nNumero entries inviato\n");
                        // FINE INVIO NUMERO ENTRIES PER GET

                    } // end HOW_MANY_ENTRIES
                    else if(strcmp(buffer, "RESPONSE_CLOSE") == 0) {
                        // SOCKET CHE CORRISPONDE AL PEER CHE TERMINA A CAUSA DELLA ESC DEL DS
                        // inizio ricezione entry
                        deleteNode(&start, getPort(start, i));
                        // decremento per capire quando far terminare il DS
                        count_peer -= 1;
                        close(i);
                        FD_CLR(i, &master);
                    } // end if buf == RESPONSE_CLOSE

                
                    if(count_peer == 0 && bool_close == 1){
                        goto epilogue;
                    }

                } // end else (socket comunicazione tcp dei singoli peer)

            } // end if ISSET
        } // end for <= fdmax

    } // end while(1)

epilogue:

    // SALVIAMO SU FILE TUTTA LA LISTA DI ENTRY
    fptr = fopen("final_entries.txt", "w");
    if(fptr == NULL)
        printf("Error opening file my_entries\n");
    else {    
        temp_entry = start_entry;
        while(temp_entry != NULL)
        {   
            fprintf(fptr, "%s %d %d %d %d\n", temp_entry->buf_date, temp_entry->porta, temp_entry->qnt_T, temp_entry->qnt_N, temp_entry->lock);
            
            temp_entry = temp_entry->next;
        }
        fclose(fptr);
    }
    
    close(boot_socket);
    boot_socket = 1;
    close(tcp_socket);
    tcp_socket = -1;
    return 0;

}
