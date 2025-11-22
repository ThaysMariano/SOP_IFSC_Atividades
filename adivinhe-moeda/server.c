//Kauan e Thays

// server.c
// Compilar com: gcc -pthread -o server server.c
// Implementa: /reset, /collect?player=NAME, /guess?id=X&value=Y&player=NAME, /status

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <time.h>

#define PORT 8080
#define N_COINS 3
#define TIME_LIMIT 30

#define STATE_FREE 0
#define STATE_OCCUPIED 1
#define STATE_DISCOVERED 2

// Tamanho máximo para o array de pontuação
#define MAX_PLAYERS 10 

typedef struct {
    int id;
    int state;
    int secret;
    char occupier_name[64]; // [NOVO] Nome do jogador que atualmente a ocupou
} Coin;

// [NOVO] Estrutura para rastrear a pontuação dos jogadores
typedef struct {
    char player_name[64];
    int score; // Número de moedas descobertas
} PlayerScore;

Coin coins[N_COINS];
sem_t coins_sem; //Controla o acesso ao recurso - limita concorrencia no numero de moedas livres
pthread_mutex_t coins_lock = PTHREAD_MUTEX_INITIALIZER; //proteger parte critica - 1 thread por vez pode alterar os estados

// [NOVO] Variáveis Globais para Pontuação
PlayerScore leaderboard[MAX_PLAYERS];
int num_players = 0;
pthread_mutex_t leaderboard_lock = PTHREAD_MUTEX_INITIALIZER; // [NOVO] Mutex para proteger o placar (leaderboard)

volatile int game_over = 0; 

//formata e envia resposta http
void json_send(int client_fd, const char *body) {
    char header[512];
    int len = snprintf(header, sizeof(header),
                       "HTTP/1.1 200 OK\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: %zu\r\n"
                       "Connection: close\r\n"
                       "\r\n", strlen(body));
    send(client_fd, header, len, 0);
    send(client_fd, body, strlen(body), 0);
}

//ver se todas as moedas foram descobertas - percorre todas
int all_discovered() {
    for (int i = 0; i < N_COINS; ++i)
        if (coins[i].state != STATE_DISCOVERED) return 0;
    return 1;
}

// [NOVO] Inicializa ou reseta a lista de pontuações
void reset_leaderboard() {
    pthread_mutex_lock(&leaderboard_lock);
    num_players = 0;
    memset(leaderboard, 0, sizeof(leaderboard));
    pthread_mutex_unlock(&leaderboard_lock);
}

void reset_game() {
    pthread_mutex_lock(&coins_lock); //pega o coins_lock para garantir acesso exclusivo
    //inicia cada moeda com free e um valor aleatorio
    for (int i = 0; i < N_COINS; ++i) { 
        coins[i].id = i;
        coins[i].state = STATE_FREE;
        coins[i].secret = rand() % 10;
        memset(coins[i].occupier_name, 0, sizeof(coins[i].occupier_name)); // [NOVO] Limpa o nome do ocupante ao resetar
    }
    sem_destroy(&coins_sem); //destroi
    sem_init(&coins_sem, 0, N_COINS);  //reinicia com 3 moedas
    game_over = 0;
    pthread_mutex_unlock(&coins_lock); //libera o coins_lock
    reset_leaderboard(); // [NOVO] Também reseta o placar
}

//limite de tempo
void *timer_thread(void *arg) {
    int id = *(int*)arg;
    free(arg);
    sleep(TIME_LIMIT); //dorme por um tempo
    pthread_mutex_lock(&coins_lock); //pega o coins_lock
    //se a moeda for ocupada libera e incrementa o semaforo (libera para outro cliente)
    if (coins[id].state == STATE_OCCUPIED) { 
        coins[id].state = STATE_FREE;
        memset(coins[id].occupier_name, 0, sizeof(coins[id].occupier_name)); // [NOVO] Limpa o nome do ocupante ao liberar por timeout
        sem_post(&coins_sem);
    }
    pthread_mutex_unlock(&coins_lock); //libera a thread para que moedas "esquecidas" por clientes sejam liberadas
    return NULL;
}

// [NOVO] Função para atualizar a pontuação de um jogador (ou adicioná-lo ao placar)
void update_player_score(const char *player_name) {
    pthread_mutex_lock(&leaderboard_lock); // Protege o acesso ao placar
    int found = 0;
    // Tenta encontrar o jogador existente
    for (int i = 0; i < num_players; i++) {
        if (strcmp(leaderboard[i].player_name, player_name) == 0) {
            leaderboard[i].score++; // Incrementa a pontuação
            found = 1;
            break;
        }
    }
    // Se não encontrou e há espaço, adiciona novo jogador
    if (!found && num_players < MAX_PLAYERS) {
        strncpy(leaderboard[num_players].player_name, player_name, 63);
        leaderboard[num_players].player_name[63] = '\0';
        leaderboard[num_players].score = 1;
        num_players++;
    }
    pthread_mutex_unlock(&leaderboard_lock);
}


void handle_collect(int client_fd, const char *player_name) { // [MOD] Adicionado player_name
    if (game_over) {
        json_send(client_fd, "{\"msg\":\"Fim de jogo! Todas as moedas descobertas.\"}");
        return;
    }

    if (sem_trywait(&coins_sem) != 0) { // [MOD] Usando trywait para dar uma resposta melhor se não houver moeda imediata
        json_send(client_fd, "{\"msg\":\"Nenhuma moeda disponivel no momento.\"}");
        return;
    }
    //sem_wait(&coins_sem);  // Bloqueava se não tivesse moedas disponíveis (sem=0) - substituído por trywait

    pthread_mutex_lock(&coins_lock); //esperar caso outra thread esteja manipulando o array
    int chosen = -1;
    //procura a primeira moeda free e coloca como ocupada
    for (int i = 0; i < N_COINS; ++i) {
        if (coins[i].state == STATE_FREE) {
            chosen = i;
            coins[i].state = STATE_OCCUPIED;
            strncpy(coins[i].occupier_name, player_name, sizeof(coins[i].occupier_name) - 1); // [NOVO] Salva o nome do ocupante
            coins[i].occupier_name[sizeof(coins[i].occupier_name) - 1] = '\0'; // Garantir terminação
            break;
        }
    }

    if (chosen == -1) {
        // sem moeda disponivel
        sem_post(&coins_sem);
        pthread_mutex_unlock(&coins_lock);
        json_send(client_fd, "{\"msg\":\"Erro interno: nenhum recurso disponivel apos sem_wait\"}");
        return;
    }

    // agora com debug: exibir secret na resposta
    char body[128];
    snprintf(body, sizeof(body),
             "{\"msg\":\"Moeda coletada por %s!\",\"coin_id\":%d,\"time_limit\":%d,\"secret\":%d}", // [MOD] Incluindo o nome na mensagem
             player_name, chosen, TIME_LIMIT, coins[chosen].secret);

    // cria timer
    pthread_t t; //thread
    int *arg = malloc(sizeof(int)); //aloca memoria 
    *arg = chosen; //coloca na memoria o id da moeda coletada
    pthread_create(&t, NULL, timer_thread, arg); //cria o timer para a moeda
    pthread_detach(t);

    pthread_mutex_unlock(&coins_lock); //fim da parte critica
    json_send(client_fd, body); //envia resposta
}


void handle_guess(int client_fd, int id, int value, const char *player_name) { // [MOD] Adicionado player_name
    pthread_mutex_lock(&coins_lock); //parte critica
    //verificacoes
    // [MOD] Verifica se a moeda está ocupada PELO jogador que está tentando o palpite
    if (id < 0 || id >= N_COINS || coins[id].state != STATE_OCCUPIED || strcmp(coins[id].occupier_name, player_name) != 0) { 
        pthread_mutex_unlock(&coins_lock);
        json_send(client_fd, "{\"result\":\"invalid or unavailable\",\"msg\":\"Moeda invalida, nao ocupada ou ocupada por outro jogador.\"}");
        return;
    }

//se acertou
    if (coins[id].secret == value) {
        coins[id].state = STATE_DISCOVERED;
        memset(coins[id].occupier_name, 0, sizeof(coins[id].occupier_name)); // [NOVO] A moeda não está mais ocupada
        //sem_post(&coins_sem); -> erro
        
        // [NOVO] Atualiza a pontuação do jogador
        update_player_score(player_name);

        int finished = all_discovered(); //se acabou
        pthread_mutex_unlock(&coins_lock); //libera

        if (finished) {
            game_over = 1;
            json_send(client_fd, "{\"result\":\"correct\",\"msg\":\"Voce acertou!\",\"msg2\":\"Fim de jogo! Todas as moedas descobertas.\"}");
        } else {
            json_send(client_fd, "{\"result\":\"correct\",\"msg\":\"Voce acertou!\"}");
        }
    } else {
        pthread_mutex_unlock(&coins_lock);
        json_send(client_fd, "{\"result\":\"wrong\",\"msg\":\"Tente novamente!\"}");
    }
}

//calcula o numero de moedas em cada estado e inclui pontuação
void handle_status(int client_fd) {
    int livres=0, ocupadas=0, descobertas=0, semval=0;
    sem_getvalue(&coins_sem, &semval); //pega valores

    pthread_mutex_lock(&coins_lock); //tranca
    //conta cada caso
    for (int i = 0; i < N_COINS; i++) {
        if (coins[i].state == STATE_FREE) livres++;
        else if (coins[i].state == STATE_OCCUPIED) ocupadas++;
        else if (coins[i].state == STATE_DISCOVERED) descobertas++;
    }
    pthread_mutex_unlock(&coins_lock); //libera

    // [NOVO] Monta a string JSON para o placar
    char leaderboard_json[1024] = "";
    char player_entry[128];
    pthread_mutex_lock(&leaderboard_lock); // Tranca o placar
    for (int i = 0; i < num_players; i++) {
        snprintf(player_entry, sizeof(player_entry), 
                 "%s\"%s\":%d", 
                 (i == 0 ? "" : ","), 
                 leaderboard[i].player_name, 
                 leaderboard[i].score);
        strncat(leaderboard_json, player_entry, sizeof(leaderboard_json) - strlen(leaderboard_json) - 1);
    }
    pthread_mutex_unlock(&leaderboard_lock); // Libera o placar

    char body[128 + 1024]; // [MOD] Aumenta o buffer para incluir o placar
    snprintf(body, sizeof(body),
             "{\"livres\":%d,\"ocupadas\":%d,\"descobertas\":%d,\"sem\":%d,\"leaderboard\":{%s}}", // [MOD] Adicionado campo 'leaderboard'
             livres, ocupadas, descobertas, semval, leaderboard_json);
    json_send(client_fd, body);
}

//reset
void handle_reset(int client_fd) {
    reset_game();
    json_send(client_fd, "{\"msg\":\"Jogo reiniciado com sucesso.\"}");
}

void serve_client(int client_fd) {
    char buf[4096];
    int r = recv(client_fd, buf, sizeof(buf)-1, 0); //recebe a req http
    if (r <= 0) { close(client_fd); return; }
    buf[r] = 0;

    char method[8], path[512];
    sscanf(buf, "%7s %511s", method, path);

//analisa qual o caminho para roteamento de funcoes
    if (strncmp(path, "/collect", 8) == 0) { // [MOD] Alterado para strncmp para checar /collect?player=NAME
        char player_name[64] = "Anonimo"; // [NOVO] Nome padrão se não for fornecido
        // [NOVO] Tenta extrair o nome do jogador da query string (ex: /collect?player=Nome)
        if (sscanf(path, "/collect?player=%63s", player_name) > 0) {} 
        handle_collect(client_fd, player_name); // [MOD] Passa o nome para a função
    } else if (strncmp(path, "/guess", 6) == 0) { // [MOD] Alterado para strncmp para checar /guess?id=X&value=Y&player=NAME
        int id, value;
        char player_name[64] = "Anonimo"; // [NOVO] Nome padrão
        // [NOVO] Tenta extrair id, value e player (ex: /guess?id=X&value=Y&player=Nome)
        if (sscanf(path, "/guess?id=%d&value=%d&player=%63s", &id, &value, player_name) == 3) {
            handle_guess(client_fd, id, value, player_name); // [MOD] Passa o nome para a função
        } else if (sscanf(path, "/guess?id=%d&value=%d", &id, &value) == 2) {
             // Caso o nome não seja passado (usa "Anonimo")
             handle_guess(client_fd, id, value, player_name); 
        } else {
            json_send(client_fd, "{\"result\":\"invalid or unavailable\"}");
        }
    } else if (strcmp(path, "/status") == 0) {
        handle_status(client_fd);
    } else if (strcmp(path, "/reset") == 0) {
        handle_reset(client_fd);
    } else {
        json_send(client_fd, "{\"msg\":\"Endpoint not found\"}");
    }

    close(client_fd); //fecha conexao
}

//para cada cliente thread - multiplas conexoes
void *client_thread(void *arg) {
    int client_fd = *(int*)arg;
    free(arg);
    serve_client(client_fd);
    return NULL;
}

int main() {
//inicia numeros aleatorios
    srand(time(NULL));
    reset_game();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT)
    };

    bind(server_fd, (struct sockaddr*)&address, sizeof(address)); //liga o socket a porta
    listen(server_fd, 10); //comeca a escutar as conexoes

///lopp infinito
    while (1) {
        socklen_t addrlen = sizeof(address);
        int client_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen); //espera novas conexoes e a cada req cria uma nova thread
        int *pclient = malloc(sizeof(int));
        *pclient = client_fd;
        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, pclient);
        pthread_detach(tid); //libera thread apos concluir
    }
    return 0;
}
