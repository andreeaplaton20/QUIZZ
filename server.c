  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <errno.h>
  #include <unistd.h>
  #include <stdio.h>
  #include <string.h>
  #include <stdlib.h>
  #include <pthread.h>
  #include <libxml/parser.h>
  #include <libxml/tree.h>
  #include <time.h>

  #define PORT 2024
  #define MAX_CLIENTI 100
  #define MAX_CLIENTI_RUNDA 3
  #define MAX_INTREB 100
  #define TIME_LIMIT 15
  #define MAX_RUNDE 10

  extern int errno;

  typedef struct {
      char intrebare[256];
      char raspuns[256];
  } Quiz;

  typedef struct {
      int socket;
      int scor;
      int activ;
      char username[50];
  } Date_clienti;

  typedef struct {
    int id_runda;
    Date_clienti clienti[MAX_CLIENTI_RUNDA];
    int nr_clienti;
    int nr_clienti_activi;
    int quizz_inceput;
    int nr_raspunsuri_primite;
    int mesaj_trimis;
    pthread_mutex_t lock;
    pthread_cond_t cond;
  } Runda;

  Quiz quiz[MAX_INTREB];
  int nr_intrebari = 0;

  Runda runde[MAX_RUNDE];

  void intrebari_din_xml(const char* filename)
  {
      xmlDoc* doc = xmlReadFile(filename, NULL, 0);
      if (doc == NULL)
      {
          fprintf(stderr, "Eroare la citirea din fisierul XML: %s\n", filename);
          exit(1);
      }

      xmlNode* root_element = xmlDocGetRootElement(doc);
      xmlNode* current = NULL;
      nr_intrebari = 0;

      for (current = root_element->children; current; current = current->next)
      {
          if (current->type == XML_ELEMENT_NODE && strcmp((char*)current->name, "question") == 0)
          {
              xmlNode* child = current->children;
              char optiuni[256] = "";
              char raspuns_corect[256] = "";

              for (; child; child = child->next)
                if (child->type == XML_ELEMENT_NODE)
                {
                  if (strcmp((char*)child->name, "text") == 0)
                    strcpy(quiz[nr_intrebari].intrebare, (char*)xmlNodeGetContent(child));
                  else if (strcmp((char*)child->name, "option") == 0)
                  {
                    strcat(optiuni, (char*)xmlNodeGetContent(child));
                    strcat(optiuni, "\n");
                  }
                  else if (strcmp((char*)child->name, "correct") == 0)
                    strcpy(raspuns_corect, (char*)xmlNodeGetContent(child));
                }
              
              strcat(quiz[nr_intrebari].intrebare, "\n");
              strcat(quiz[nr_intrebari].intrebare, optiuni);

              strcpy(quiz[nr_intrebari].raspuns, raspuns_corect);

              nr_intrebari++;
              if (nr_intrebari >= MAX_INTREB) break;
          }
      }
      xmlFreeDoc(doc);
      printf("server: Intrebarile au fost incarcate. Total: %d\n", nr_intrebari);
  }

  void trimite_clasament(Runda* runda)
  {
    char clasament[1024] = "Clasament final:\n";
    int scoruri[MAX_CLIENTI_RUNDA];

    for (int i = 0; i < runda->nr_clienti; i++)
      scoruri[i] = runda->clienti[i].scor;

    for (int i = 0; i < runda->nr_clienti - 1; i++)
      for (int j = i + 1; j < runda->nr_clienti; j++)
        if (scoruri[i] < scoruri[j])
        {
          int temp_scor = scoruri[i];
          scoruri[i] = scoruri[j];
          scoruri[j] = temp_scor;
        }

    for (int i = 0; i < runda->nr_clienti; i++)
    {
      char linie[256];
      snprintf(linie, sizeof(linie), "Locul %d: Jucator %s - Scor: %d\n", i+1, runda->clienti[i].username, runda->clienti[i].scor);
      strcat(clasament, linie);
    }

    for (int i = 0; i < runda->nr_clienti; i++)
      if (runda->clienti[i].activ)
      {
        printf("Client %s cu scorul %d\n", runda->clienti[i].username, runda->clienti[i].scor);
        if (write(runda->clienti[i].socket, clasament, strlen(clasament) + 1) <= 0)
          perror("Server: Eroare la trimiterea clasamentului!\n");
      }
  }

  Runda* gaseste_runda(const char* username)
  {
    for (int i = 0; i < MAX_RUNDE; i++)
      for (int j = 0; j < runde[i].nr_clienti; j++)
        if (strcmp(runde[i].clienti[j].username, username) == 0)
          return &runde[i];
    return NULL;
  }

  void* client_handler(void* arg)
  {
    Date_clienti* client = (Date_clienti*)arg;
    char buffer[256];
    
    Runda* runda = gaseste_runda(client->username);

    if (!runda)
    {
      fprintf(stderr, "Eroare: Nu s-a gasit runda pentru client!\n");
      pthread_exit(NULL);
    }

    while (1)
    {
      pthread_mutex_lock(&runda->lock);
      while(!runda->quizz_inceput)
        pthread_cond_wait(&runda->cond, &runda->lock);
      pthread_mutex_unlock(&runda->lock);
      

      for (int i = 0; i < nr_intrebari; i++)
      {
        bzero(buffer, sizeof(buffer));
        strcpy(buffer, quiz[i].intrebare);
        if (write(client->socket, buffer, strlen(buffer) + 1) <= 0)
        {
          perror("Server: Eroare la trimiterea intrebarii!\n");
          close(client->socket);
          pthread_exit(NULL);
        }

        bzero(buffer, sizeof(buffer));
        int bytes_read = read(client->socket, buffer, sizeof(buffer));
        if (bytes_read == 0) 
        { 
          printf("Server: Client %s a închis conexiunea.\n", client->username);
          pthread_exit(NULL);
        } 
        else if (bytes_read < 0) 
        {
          perror("Server: Eroare la citirea de la client.\n");
          pthread_exit(NULL);
        }

        buffer[strcspn(buffer, "\n")] = '\0';
        printf("Server: Raspuns de la client %s: %s\n", client->username, buffer);

        pthread_mutex_lock(&runda->lock);
        if (strcmp(buffer, "QUIT") == 0)
        {
          printf("Server: Client %s a dat QUIT.\n", client->username);

          client->activ = 0;
          runda->nr_clienti_activi--;
          runda->nr_raspunsuri_primite--;
          printf("clientii activi: %d\n", runda->nr_clienti_activi);

          if (runda->nr_clienti_activi == 0)
          {
            runda->quizz_inceput = 0;
            pthread_cond_broadcast(&runda->cond);
          }
        }

        else if (strcmp(buffer, quiz[i].raspuns) == 0)
        {
          printf("Server: Raspuns corect!\n");
          client->scor++;
        } else {
          printf("Raspunsul corect este: %s\n", quiz[i].raspuns);
          printf("Server: raspuns gresit!\n");
        }

        runda->nr_raspunsuri_primite++;
        if (runda->nr_raspunsuri_primite == runda->nr_clienti_activi)
        {
          runda->nr_raspunsuri_primite = 0;
          pthread_cond_broadcast(&runda->cond);
        }
        else 
          while (runda->nr_raspunsuri_primite > 0)
            pthread_cond_wait(&runda->cond, &runda->lock);

        pthread_mutex_unlock(&runda->lock);
      }

      pthread_mutex_lock(&runda->lock);
      if (!runda->mesaj_trimis)
      {
        runda->mesaj_trimis = 1;
        for (int i = 0; i < runda->nr_clienti; i++)
          if (runda->clienti[i].socket > 0 && runda->clienti[i].activ)
            //printf("Mesaj de stop pentru clientul %s\n", client->username);
            if (write(runda->clienti[i].socket, "STOP", 5) <= 0)
              perror("Server: Eroare la trimiterea mesajului de sfarsit!\n");
      }

      if (runda->quizz_inceput)
      {
        runda->quizz_inceput = 0;
        trimite_clasament(runda);
      }
      pthread_mutex_unlock(&runda->lock);
      break;
    }
    close(client->socket);
    pthread_exit(NULL);
  }

  int main ()
  {
    struct sockaddr_in server, from;
    int sd;

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
      perror("Server: Eroare la socket()!\n");
      return errno;
    }

    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    if (bind(sd, (struct sockaddr*)&server, sizeof(struct sockaddr)) == -1)
    {
      perror("Eroare la bind()!\n");
      return errno;
    }

    if (listen(sd, MAX_CLIENTI_RUNDA) == -1)
    {
      perror("Eroare la listen()!\n");
      close(sd);
      return errno;
    }

    printf("Server: Asteptam conexiuni...\n");

    intrebari_din_xml("intrebari.xml");

    for (int i = 0; i < MAX_RUNDE; i++)
    {
      pthread_mutex_init(&runde[i].lock, NULL);
      pthread_cond_init(&runde[i].cond, NULL);
      runde[i].id_runda = i;
      runde[i].nr_clienti = 0;
      runde[i].nr_clienti_activi = 0;
      runde[i].quizz_inceput = 0;
      runde[i].nr_raspunsuri_primite = 0;
      runde[i].mesaj_trimis = 0;
    }

    while(1)
    {
      int client;
      socklen_t length = sizeof(from);

      client = accept(sd, (struct sockaddr*)&from, &length);

      if (client < 0)
      {
        perror("Eroare la accept()!\n");
        continue;
      }

      char username[50];
      bzero(username, sizeof(username));
      int bytes_read = read(client, username, sizeof(username)-1);
      if (bytes_read <= 0)
      {
        printf("Eroare la citierea username-ului!\n");
        close(client);
        continue;
      }
      username[strcspn(username, "\n")] = '\0';

      int runda_asignata = -1;
      for (int i = 0; i < MAX_RUNDE; i++)
        if (runde[i].nr_clienti < MAX_CLIENTI_RUNDA)
        {
          runda_asignata = i;
          break;
        }

      if (runda_asignata == -1)
      {
        printf("Server: Toate rundele sunt pline. Client refuzat.\n");
        close(client);
        continue;
      }

      Runda* runda = &runde[runda_asignata];

      pthread_mutex_lock(&runda->lock);
      int client_id = runda->nr_clienti;

      runda->clienti[client_id].socket = client;
      runda->clienti[client_id].scor = 0;
      runda->clienti[client_id].activ = 1;
      strncpy(runda->clienti[client_id].username, username, sizeof(runda->clienti[client_id].username));

      runda->nr_clienti++;
      runda->nr_clienti_activi++;

      if (runda->nr_clienti == MAX_CLIENTI_RUNDA)
      {
        runda->quizz_inceput = 1;
        pthread_cond_broadcast(&runda->cond);
        printf("Server: Semnal de inceput trimis.\n");
      }
      pthread_mutex_unlock(&runda->lock);

      pthread_t thread;
      pthread_create(&thread, NULL, client_handler, &runda->clienti[client_id]);
    }

    return 0;
  }	
