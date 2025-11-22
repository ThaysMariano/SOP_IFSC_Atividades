#ifndef DADOS_H
#define DADOS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>

#define MQ_NAME "/fila_sensor"  // nome da fila POSIX
#define PORTA 8080

typedef struct {
    float temperatura;
    int contador;
    char status[16]; // "NORMAL" ou "ALERTA"
} dado_t;

#endif

