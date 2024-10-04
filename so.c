#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>

/*
ele ta alternando entre os processos, mas acredito que não esteja continuando o processo de verdade
ele só printa a linha de "processo: executando, PC=" uma vez
aumentei o tempo de TIME_SLICE para visualizar melhor, mas tem que voltar depois para 0.5
boa noite
*/

#define NUM_PROCESSES 3     // num de processos
#define NUM_DEVICES 2      // num de devices
#define MAX_ITER 10       // maximo de interacoes dos procesos
#define TIME_SLICE 5     // 500ms

int blocked_process[NUM_DEVICES][NUM_PROCESSES]; // fila de processos bloqueados
pid_t processes[NUM_PROCESSES];                 // array de PIDs
int program_counter[NUM_PROCESSES];            // contador cada processo
int current_process = 0;                      // processo em execucao
int pipes[NUM_PROCESSES][2];                 // descritor de pipes para comunicação

void D1_handler(int signum){
    for (int i = 0; i < NUM_PROCESSES; i++){
        if (blocked_process[0][i] == 1){
            blocked_process[0][i] = 0;
            write(pipes[i][1], "D1", 3);  // Notifica o processo via pipe
            printf("Processo %d desbloqueado pelo dispositivo D1\n", i);
            return;
        }
    }
    printf("D1.\n");
}

void D2_handler(int signum){
    for (int i = 0; i < NUM_PROCESSES; i++){
        if (blocked_process[1][i] == 1){
            blocked_process[1][i] = 0;
            write(pipes[i][1], "D2", 3);  // Notifica o processo via pipe
            printf("Processo %d desbloqueado pelo dispositivo D2\n", i);
            return;
        }
    }
    printf("D2.\n");
}

void TS_handler(int signum){
    printf("Time slice.\n");
}

void sigcont_handler(int signum) {
    printf("Processo %d: Retomando execução\n", getpid());
}

void sigstop_handler(int signum) {
    printf("Processo %d: Pausado pelo Kernel\n", getpid());
}

void syscall_sim(int device, char* operation) {
    /*
    Simula uma chamada para o kernel de término de processo
    */
    if (strcmp(operation,"Read") == 0) {
        kill(current_process, SIGUSR1);
        printf("Dispositivo D%d: Solicitação de Read\n", device);
    }
    else if (strcmp(operation,"Write") == 0) {
        kill(current_process, SIGUSR2);
        printf("Dispositivo D%d: Solicitação de Write\n", device);
    }
    printf("Processo %d bloqueado no dispositivo D%d\n", current_process, device);
}


void process_application(int id) {
    int device, chance;
    char* operation;

    while (program_counter[id] < MAX_ITER) {
        printf("Processo %d: Executando, PC=%d\n", id, program_counter[id]);
        program_counter[id]++;
        sleep(1);  // Simulação de trabalho (1 segundo por iteração)

        chance = rand() % 100;  // gera uma chance para syscall

        if (chance < 15) {  // Simula chance de fazer uma syscall (15% de chance)
            device = (rand() % NUM_DEVICES);  // randomiza o dispositivo (D1 ou D2)
            if (chance % 3 == 0) operation = "Read";
            else if (chance % 3 == 1) operation = "Write";
            else operation = "X";

            syscall_sim(device, operation);
            blocked_process[device][id] = 1;

            printf("Processo %d: Realizando syscall, entrando em espera...\n", id);
            kill(id, SIGSTOP);  // Pausa o processo
            pause();  // Simula bloqueio por syscall
        }

        // Verifica se há mensagens no pipe
        char buffer[4];
        int bytes_read = read(pipes[id][0], buffer, sizeof(buffer));
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';  // Certifique-se de que a string é terminada em nulo
            if (strcmp(buffer, "D1") == 0 || strcmp(buffer, "D2") == 0) {
                printf("Processo %d: Recebeu notificação de desbloqueio do dispositivo %s\n", id, buffer);
                blocked_process[device][id] = 0;  // Desbloqueia o processo
            }
        }
    }

    printf("Processo %d: Finalizado\n", id);
    exit(0);
}


void create_processes() {
    signal(SIGCONT, sigcont_handler);  // Define o handler para SIGCONT
    signal(SIGSTOP, sigstop_handler);  // Define o handler para SIGSTOP

    for (int i = 0; i < NUM_PROCESSES; i++) {
        program_counter[i] = 0; 
        pipe(pipes[i]);   // Cria um pipe para cada processo
        processes[i] = fork();   // Cria o processo filho

        if (processes[i] == 0) {
            // Processo filho executa o código da aplicação
            process_application(i);
        }
    }
}

void kernel_sim() {  
    /*
    Gerencia 3 a 5 processos e intercala suas execuções
    a depender se estão esperando pelo término de uma operação
    de leitura ou escrita em um dispositivo E/S simulado (D1 e D2)
    ou o sinal de aviso do término de sua fatia de tempo
    */
    struct timeval time;
    int current_time;
    int previous_time = 0;

    create_processes();  // Cria os processos de aplicação 

    while(1) {
        // Verifica se todos os processos já terminaram
        int all_finished = 1;
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (program_counter[i] < MAX_ITER) {
                all_finished = 0;  // Se algum processo não terminou, setar como não finalizado
                break;
            }
        }

        if (all_finished) {
            printf("Todos os processos finalizaram. Encerrando o kernel.\n");
            break;  // Sai do loop se todos os processos já terminaram
        }

        printf("Kernel: Executando processo %d\n", current_process);
        kill(processes[current_process], SIGCONT); // Retoma a execução do processo atual
        sleep(TIME_SLICE); // Espera pelo time slice

        // Após o time slice, pausa o processo
        kill(processes[current_process], SIGSTOP); // Pausa o processo
        printf("Kernel: Pausando processo %d\n", current_process);

        current_process = (current_process + 1) % NUM_PROCESSES;  // Seleciona o próximo processo
    }
}


void controller_sim(){
    /*
    Emula o controlador de interrupções que gera as interrupções
    referentes ao relógio (IRQ0) e ao término da operação de E/S
    no D1 (IRQ1) e D2 (IRQ2)

    O time slice é de 500 ms
    I/O do D1 é random
    I/O do D2 é random
    */
    while (1) {
        sleep(TIME_SLICE);  // IRQ0 a cada 500ms
        kill(getppid(), SIGRTMIN);  // Envia IRQ0 (time slice)

        // IRQ1 tem probabilidade de 0.1
        if ((rand() % 100) < 10) {
            kill(getppid(), SIGUSR1);  // Envia IRQ1 (D1)
        }

        // IRQ2 tem probabilidade de 0.05
        if ((rand() % 100) < 5) {
            kill(getppid(), SIGUSR2);  // Envia IRQ2 (D2)
        }
    }
}

int main() {
    signal(SIGRTMIN, TS_handler);  // sinal para o time slice
    signal(SIGUSR1, D1_handler);   // sinal para o D1
    signal(SIGUSR2, D2_handler);   // sinal para o D2

    pid_t ICS_pid = fork();  // criando o processo do InterController

    if (ICS_pid == 0) {
        controller_sim();  // Inicia a simulação do InterController
    }
    else {
        pid_t kernel_pid = fork();

        if (kernel_pid == 0) {
            kernel_sim();  // Inicia o kernel de escalonamento
        }
    }

    // Aguarda o término dos processos filhos
    while (wait(NULL) > 0);

    return 0;
}
