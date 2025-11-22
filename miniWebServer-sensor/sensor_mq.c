/*
 * MiniProjeto IPC Processo SENSOR (Fila de Mensagens POSIX)
 * Dupla: _______________________
 *
 * Objetivo: gerar leituras de temperatura e repassar
 * para o processo servidor web via fila de mensagens POSIX.
 */

#include "dados.h"
#include <fcntl.h>   // O_CREAT, O_RDWR
#include <sys/stat.h>

int main() {
    srand(time(NULL));

    // Atributos da fila
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(dado_t);
    attr.mq_curmsgs = 0;

    // Cria ou abre a fila
    mqd_t mq = mq_open(MQ_NAME, O_CREAT | O_WRONLY, 0644, &attr);
    if (mq == -1) {
        perror("Erro ao criar fila");
        exit(1);
    }

    printf("[Sensor] Iniciando geração de leituras...\n");

    int contador = 0;
    while (1) {
        dado_t dado;
        dado.temperatura = 20 + (rand() % 1500) / 100.0; // entre 20.00 e 35.00
        dado.contador = contador++;
        strcpy(dado.status, (dado.temperatura > 30.0) ? "ALERTA" : "NORMAL");

        // Envia o dado pela fila
        if (mq_send(mq, (char *)&dado, sizeof(dado_t), 0) == -1) {
            perror("Erro ao enviar mensagem");
        } else {
            printf("[Sensor] Nova leitura: %.2f°C (%s)\n",
                   dado.temperatura, dado.status);
        }
	sleep(1);
    }

    mq_close(mq);
    mq_unlink(MQ_NAME); // remove a fila ao sair
    return 0;
}

