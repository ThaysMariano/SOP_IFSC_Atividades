/*
 * MiniProjeto IPC Processo SERVIDOR WEB (Fila de Mensagens POSIX)
 * Dupla: _______________________
 *
 * Objetivo: atender clientes HTTP e exibir dados de temperatura
 * recebidos do processo sensor via fila de mensagens POSIX.
 */

#include "dados.h"
#include <fcntl.h>
#include <sys/stat.h>

dado_t ultimo_dado;
pthread_mutex_t dado_mutex = PTHREAD_MUTEX_INITIALIZER;

// Thread para ler dados da fila de mensagens continuamente
void *recebe_dados(void *arg) {
    mqd_t mq = mq_open(MQ_NAME, O_RDONLY);
    if (mq == -1) {
        perror("Erro ao abrir fila no servidor");
        pthread_exit(NULL);
    }

    struct mq_attr attr;
    if (mq_getattr(mq, &attr) == -1) {
        perror("Erro ao obter atributos da fila");
        pthread_exit(NULL);
    }

    // Aloca buffer com o tamanho correto
    dado_t *dado = malloc(attr.mq_msgsize);

    while (1) {
        ssize_t bytes = mq_receive(mq, (char *)dado, attr.mq_msgsize, NULL);
        if (bytes >= 0) {
            pthread_mutex_lock(&dado_mutex);
            ultimo_dado = *dado;
            pthread_mutex_unlock(&dado_mutex);
            printf("[WebServer] Recebido: %.2f°C (%s)\n",
                   dado->temperatura, dado->status);
        } else {
            perror("Erro ao receber mensagem");
            sleep(1);
        }
    }

    free(dado);
    mq_close(mq);
    pthread_exit(NULL);
}


// Thread para atender um cliente HTTP
void *atende_cliente(void *arg) {
    int cliente = *(int *)arg;
    free(arg);

    pthread_mutex_lock(&dado_mutex);
    dado_t copia = ultimo_dado;
    pthread_mutex_unlock(&dado_mutex);

    // Define cor e ícone conforme status
    const char *cor_status = (strcmp(copia.status, "ALERTA") == 0) ? "#e74c3c" : "#27ae60";
    const char *emoji = (strcmp(copia.status, "ALERTA") == 0) ? "⚠️" : "✅";

    // Gera a resposta HTML com estilos
    char resposta[2048];
    snprintf(resposta, sizeof(resposta),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n\r\n"
        "<!DOCTYPE html><html lang='pt-br'>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='2'>" // atualiza a cada 2 segundos
        "<title>Monitor de Temperatura</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; background:#f4f4f9; display:flex; justify-content:center; align-items:center; margin-top:50px; }"
        ".painel { display:inline-block; background:white; padding:40px; border-radius:20px; "
        "box-shadow:0 0 15px rgba(0,0,0,0.1); min-width:300px; }"
        "h1 { color:#2c3e50; }"
        ".temp { font-size:3em; font-weight:bold; color:%s; margin:10px 0; }"
        ".status { font-size:1.5em; color:%s; font-weight:bold; }"
        "</style></head>"
        "<body>"
        "<div class='painel'>"
        "<h1>Servidor de Temperatura</h1>"
        "<div class='temp'>%.2f °C</div>"
        "<p>Leitura #%d</p>"
        "<div class='status'>%s %s</div>"
        "</div>"
        "</body></html>",
        cor_status, cor_status,
        copia.temperatura, copia.contador, emoji, copia.status
    );

    send(cliente, resposta, strlen(resposta), 0);
    close(cliente);
    pthread_exit(NULL);
}


int main() {
    pthread_t t_recebe;
    pthread_create(&t_recebe, NULL, recebe_dados, NULL);

    int servidor = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in end = {0};
    end.sin_family = AF_INET;
    end.sin_port = htons(PORTA);
    end.sin_addr.s_addr = INADDR_ANY;

    bind(servidor, (struct sockaddr *)&end, sizeof(end));
    listen(servidor, 5);

    printf("[WebServer] Servindo na porta %d...\n", PORTA);

    while (1) {
        int *cliente = malloc(sizeof(int));
        *cliente = accept(servidor, NULL, NULL);
        pthread_t t;
        pthread_create(&t, NULL, atende_cliente, cliente);
        pthread_detach(t);
    }

    close(servidor);
    mq_unlink(MQ_NAME);
    return 0;
}

