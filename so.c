#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#define NUM_PROCESSES 3 // num de processos
#define MAX_ITER 10        // maximo de interacoes dos procesos
#define TIME_SLICE 500000 // 500ms (usleep usa microsegundos) 

pid_t processes[NUM_PROCESSES];     // array de Pids
int program_counter[NUM_PROCESSES]; // contador cada processo
int current_process = 0;            // processo em execucao


void sigcont_handler(int signum) {
    printf("Processo %d: Retomando execução\n", getpid());
}

void sigstop_handler(int signum) {
    printf("Processo %d: Pausado pelo Kernel\n", getpid());
    pause();  // processo esperando retomada 
}

void process_application(int id) {
    signal(SIGCONT, sigcont_handler); // Define o handler para SIGCONT
    signal(SIGSTOP, sigstop_handler); // Define o handler para SIGSTOP

    while (program_counter[id] < MAX_ITER) {
        printf("Processo %d: Executando, PC=%d\n", id, program_counter[id]);
        program_counter[id]++;
        usleep(100000); // Simulação de trabalho (100ms por iteração)

        if (rand() % 100 < 15) {
             // Simula chance de fazer uma syscall (15% de chance)
            printf("Processo %d: Realizando syscall, entrando em espera...\n", id);
            pause(); // Simula bloqueio por syscall
        }
    }

    printf("Processo %d: Finalizado\n", id);
    exit(0);
}

void create_processes() {
    for (int i = 0; i < NUM_PROCESSES; i++) {
        program_counter[i] = 0; 
        processes[i] = fork();   // Cria o processo filho

        if (processes[i] == 0) {
            // Processo filho executa o código da aplicação
            process_application(i);
        }
    }
}

void kernel_sim() {
    struct timespec ts = {0, TIME_SLICE}; // Time slice de 500ms

    while (1) {
        printf("Kernel: Executando processo %d\n", current_process);
        kill(processes[current_process], SIGCONT); // Continua o processo atual

        nanosleep(&ts, NULL); // Espera o time slice (500ms)

        // Pausa o processo atual e seleciona o próximo
        kill(processes[current_process], SIGSTOP); // Pausa o processo
        current_process = (current_process + 1) % NUM_PROCESSES; // Seleciona o próximo processo
    }
}

int main() {
    srand(time(NULL)); // Inicializa o gerador de números aleatórios
    create_processes(); // Cria os processos de aplicação
    kernel_sim();       // Inicia a simulação do Kernel

    return 0;
}
