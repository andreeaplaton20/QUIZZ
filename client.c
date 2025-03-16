#include <gtk/gtk.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <pthread.h>

extern int errno;

int port;
int sd;
GtkWidget *label_timer;
GtkWidget *entry_response;
GtkWidget *label_question;
GtkWidget *window;
GtkWidget *label_clasament;
GtkWidget *entry_username;
GtkWidget *button_start;
GtkWidget *vbox_main;
GtkWidget *button_quit;
GtkWidget *button_send;

int timer = 20;
guint timer_id;

void on_send_answer(GtkWidget *widget, gpointer data)
{
  const char *raspuns = gtk_entry_get_text(GTK_ENTRY(entry_response));
  write(sd, raspuns, strlen(raspuns) + 1);
  printf("Client: Raspuns trimis catre server: %s\n", raspuns);
}

void on_send_quit(GtkWidget *widget, gpointer data)
{
  write(sd, "QUIT", 5);
  printf("Client: Quizul s-a terminat! Inchid conexiunea!\n");
  close(sd);
  gtk_widget_destroy(window);
  gtk_main_quit();
  exit(0);
}

void on_leave_button(GtkWidget *widget, gpointer data)
{
  printf("Client: Utilizatorul a apasat butonul de leave.\n");
  gtk_widget_destroy(window);
  gtk_main_quit();
  exit(0);
}

void stop_timer()
{
  if (timer_id != 0)
  {
    g_source_remove(timer_id);
    timer_id = 0;
  }
  gtk_label_set_text(GTK_LABEL(label_timer), "STOP");
}

gboolean update_timer(gpointer data)
{
  if (timer > 0)
  {
    char timer_text[10];
    snprintf(timer_text, sizeof(timer_text), "%d", timer);
    gtk_label_set_text(GTK_LABEL(label_timer), timer_text);
    timer--;
  }
  else
  {
    const char *default_answer = "0\n";
    write(sd, default_answer, strlen(default_answer) + 1);
    printf("Client: Timpul a expirat! Trimit raspunsul implicit: 0\n");
    stop_timer();
  }
  return timer > 0;
}

void switch_to_quizz_interface()
{
  gtk_container_foreach(GTK_CONTAINER(vbox_main), (GtkCallback)gtk_widget_destroy, NULL);

  label_question = gtk_label_new("Astept intrebarea...");
  gtk_box_pack_start(GTK_BOX(vbox_main), label_question, FALSE, FALSE, 5);
  gtk_widget_set_valign(label_question, GTK_ALIGN_CENTER);

  label_timer = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(vbox_main), label_timer, FALSE, FALSE, 5);
  gtk_widget_set_valign(label_question, GTK_ALIGN_CENTER);

  entry_response = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(vbox_main), entry_response, FALSE, FALSE, 5);
  gtk_entry_set_alignment(GTK_ENTRY(entry_response), 0.5);

  button_send = gtk_button_new_with_label("Trimite raspuns");
  gtk_box_pack_start(GTK_BOX(vbox_main), button_send, FALSE, FALSE, 5);
  g_signal_connect(button_send, "clicked", G_CALLBACK(on_send_answer), NULL);

  button_quit = gtk_button_new_with_label("Quit");
  gtk_box_pack_start(GTK_BOX(vbox_main), button_quit, FALSE, FALSE, 5);
  g_signal_connect(button_quit, "clicked", G_CALLBACK(on_send_quit), NULL);

  gtk_widget_show_all(window);
}

gboolean switch_to_clasament_interface(gpointer data)
{
  const char *clasament = (const char *)data;
  gtk_container_foreach(GTK_CONTAINER(vbox_main), (GtkCallback)gtk_widget_destroy, NULL);

  GtkWidget *label_runda_incheiata = gtk_label_new("Runda s-a incheiat!");
  gtk_box_pack_start(GTK_BOX(vbox_main), label_runda_incheiata, FALSE, FALSE, 5);
  gtk_widget_set_valign(label_runda_incheiata, GTK_ALIGN_CENTER);

  label_clasament = gtk_label_new(clasament);
  gtk_box_pack_start(GTK_BOX(vbox_main), label_clasament, FALSE, FALSE, 5);
  gtk_widget_set_valign(label_clasament, GTK_ALIGN_CENTER);

  GtkWidget *label_thank_you = gtk_label_new("Multumim pentru participare!");
  gtk_box_pack_start(GTK_BOX(vbox_main), label_thank_you, FALSE, FALSE, 5);
  gtk_widget_set_valign(label_thank_you, GTK_ALIGN_CENTER);

  GtkWidget *button_leave = gtk_button_new_with_label("Leave");
  gtk_box_pack_start(GTK_BOX(vbox_main), button_leave, FALSE, FALSE, 5);
  g_signal_connect(button_leave, "clicked", G_CALLBACK(on_leave_button), NULL);

  gtk_widget_show_all(window);

  g_free(data);
  return FALSE;
}

void on_start_clicked(GtkWidget *widget, gpointer data)
{
  const char *username = gtk_entry_get_text(GTK_ENTRY(entry_username));
  if (strlen(username) == 0)
  {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "Introduceti un username:");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return;
  }
  if (write(sd, username, strlen(username) + 1) <= 0)
  {
    perror("Client: Eroare la trimiterea username-ului");
    close(sd);
    exit(1);
  }
  switch_to_quizz_interface();
}

void citire_clasament(int sd)
{
  char buffer[1024];
  char *clasament = g_malloc0(4096);
  int bytes_read;

  do
  {
    bzero(buffer, sizeof(buffer));
    bytes_read = read(sd, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0)
      strcat(clasament, buffer);
  } while (bytes_read > 0 && !strstr(buffer, "Clasament final:"));

  printf("Client: Clasamentul a fost receptionat complet!\n");

  g_idle_add((GSourceFunc)switch_to_clasament_interface, clasament);
}

gboolean update_question(gpointer data)
{
  const char *question = (const char *)data;
  gtk_label_set_text(GTK_LABEL(label_question), question);
  timer = 30;
  char timer_text[10];
  snprintf(timer_text, sizeof(timer_text), "%d", timer);
  gtk_label_set_text(GTK_LABEL(label_timer), timer_text);
  gtk_entry_set_text(GTK_ENTRY(entry_response), "");
  
  if (timer_id != 0)
    g_source_remove(timer_id);
  timer_id = g_timeout_add(1000, update_timer, NULL);

  g_free(data);
  return FALSE;
}

void *server_listener(void *arg)
{
  char msg[1024];
  while(1)
  {
    bzero(msg, sizeof(msg));

    int bytes_read = read(sd, msg, sizeof(msg) - 1);
    if (bytes_read < 0)
    {
      perror("Client: Eroare la citirea intrebarii de la server!\n");
      close(sd);
      return NULL;
    }
    else if (bytes_read == 0)
    {
      printf("Client: Conexiunea cu serverul a fost inchisa.\n");
      break;
    }

    if (strncmp(msg, "STOP", 5) == 0)
    {
      printf("Client: Quizz-ul s-a terminat!\n");
      stop_timer();
      citire_clasament(sd);
      break;
    }
    else
    {
      printf("Intrebare primita: %s\n", msg);
      g_idle_add((GSourceFunc)update_question, g_strdup(msg));
    }
  }
  return NULL;
}

void apply_css()
{
  GtkCssProvider *provider = gtk_css_provider_new();
  GdkDisplay *display = gdk_display_get_default();
  GdkScreen *screen = gdk_display_get_default_screen(display);

  const gchar *css_data = 
    "window {"
    "    background-color:  #004d00;"
    "}"
    "label {"
    "    font-family: 'Oswald', sans-serif;"
    "    font-weight: bold;"
    "    font-size: 16px;"
    "    color: #f0f8f0;"
    "    margin: 60px;"
    "    margin-top: 10px;"
    "    margin-bottom: 10px;"
    "}"
    "entry {"
    "    font-family: 'Courier New', monospace;"
    "    font-weight: bold;"
    "    font-size: 18px;"
    "    color: #006400;"
    "    border: 2px solid #ced4da;"
    "    padding: 7px;"
    "    margin-left: 70px;"
    "    margin-right: 70px;"
    "    background-color: #ffffff;"
    "}"
    "button {"
    "    background-color: #4CAF50;"
    "    border: none;"
    "    border-radius: 15px;"
    "    padding: 8px 12px;"
    "    margin: 135px;"
    "    margin-top: 10px;"
    "    margin-bottom: 10px;"
    "    transition: background-color 0.3s;"
    "}"
    "button:hover {"
    "    background-color: #006400;"
    "}";
  
  gtk_css_provider_load_from_data(provider, css_data, -1, NULL);
  gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  g_object_unref(provider);
}

int main(int argc, char *argv[])
{
  if (gtk_init_check(&argc, &argv) == FALSE)
  {
    fprintf(stderr, "Eroare la initializarea GTK.\n");
    return -1;
  }

  apply_css();

  if (argc != 3)
  {
    printf("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
    return -1;
  }

  port = atoi(argv[2]);

  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("Eroare la socket().\n");
    return errno;
  }

  struct sockaddr_in server;
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = inet_addr(argv[1]);
  server.sin_port = htons(port);

  if (connect(sd, (struct sockaddr*)&server, sizeof(struct sockaddr)) == -1)
  {
    perror("Client: Eroare la connect()!\n");
    return errno;
  }

  printf("Client: Conexiunea a fost stabilita cu succes!\n");

  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window), "Quizz Game");
  gtk_window_set_default_size(GTK_WINDOW(window), 400, 200);

  vbox_main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_container_add(GTK_CONTAINER(window), vbox_main);

  GtkWidget *label_welcome = gtk_label_new("Bun venit! Introduceti username-ul pentru a incepe:");
  gtk_box_pack_start(GTK_BOX(vbox_main), label_welcome, FALSE, FALSE, 5);
  gtk_widget_set_valign(label_welcome, GTK_ALIGN_CENTER);

  entry_username = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(vbox_main), entry_username, FALSE, FALSE, 5);
  gtk_entry_set_alignment(GTK_ENTRY(entry_username), 0.5);

  button_start = gtk_button_new_with_label("Start");
  gtk_box_pack_start(GTK_BOX(vbox_main), button_start, FALSE, FALSE, 5);
  g_signal_connect(button_start, "clicked", G_CALLBACK(on_start_clicked), NULL);

  g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

  gtk_widget_show_all(window);

  pthread_t listener_thread;
  if (pthread_create(&listener_thread, NULL, server_listener, NULL) != 0)
  {
    perror("Client: Eroare la crearea thread-ului pentru ascultarea serverului.\n");
    return -1;
  }
  gtk_main();

  printf("Client: Conexiunea se inchide. Multumim pentru participare!\n");
  close(sd);

  return 0;
}