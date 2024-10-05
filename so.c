#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

/*
ele ta alternando entre os processos, mas acredito que não esteja continuando o processo de verdade
ele só printa a linha de "processo: executando, PC=" uma vez
aumentei o tempo de TIME_SLICE para visualizar melhor, mas tem que voltar depois para 0.5
boa noite
*/

#define NUM_PROCESSES 3     // num de processos
#define NUM_DEVICES 2      // num de devices
#define MAX_ITER 10       // maximo de interacoes dos procesos
#define TIME_SLICE 3     // 500ms

int blocked_process[NUM_DEVICES][NUM_PROCESSES]; // fila de processos bloqueados
pid_t processes[NUM_PROCESSES];                 // array de PIDs
int program_counter[NUM_PROCESSES];            // contador cada processo
int *current_process;                         // processo em execucao (memoria compartilhada)
int pipes[NUM_PROCESSES][2];                 // descritor de pipes para comunicação
__sighandler_t cont;
__sighandler_t stop;

void D1_handler(int signum) {
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (blocked_process[0][i] == 1) {
            blocked_process[0][i] = 0;
            kill(processes[i], SIGCONT); // Retoma o processo diretamente
            printf("Processo %d desbloqueado pelo dispositivo D1\n", i);
            return;
        }
    }
}

void D2_handler(int signum) {
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (blocked_process[1][i] == 1) {
            blocked_process[1][i] = 0;
            kill(processes[i], SIGCONT); // Retoma o processo diretamente
            printf("Processo %d desbloqueado pelo dispositivo D2\n", i);
            return;
        }
    }
}

void TS_handler(int signum) {

}

void sigcont_handler(int signum) {
    printf("Processo %d: Retomando execução\n", *current_process);
    cont(signum);
}

void sigstop_handler(int signum) {
    printf("Processo %d: Pausado pelo Kernel\n", *current_process);
    stop(signum);
}

void syscall_sim(int device, char* operation) {
    /*
        Simula uma chamada para o kernel de término de processo
    */
    printf("Dispositivo D%d: Solicitação de %s \n", device, operation);
    if (device == 0) {
        kill(*current_process, SIGUSR1);
    } 
    else if (device == 1) {
        kill(*current_process, SIGUSR2);
    }
    printf("Processo %d bloqueado no dispositivo D%d\n", *current_process, device);
}

void process_application(int id) {
    cont = signal(SIGCONT, sigcont_handler); // Define o handler para SIGCONT
    pause();
    stop = signal(SIGSTOP, sigstop_handler); // Define o handler para SIGSTOP
    int device, chance;
    char* operation;

    while (program_counter[id] < MAX_ITER) {
        printf("Processo %d: executando, PC=%d\n", id, program_counter[id]);
        program_counter[id]++;
        sleep(1);

        chance = rand() % 100; // gera uma chance para syscall

        if (chance < 15) { // Simula chance de fazer uma syscall (15% de chance)
            device = (rand() % NUM_DEVICES);  // randomiza o dispositivo (D1 ou D2)
            if (chance % 3 == 0) operation = "Read";
            else if (chance % 3 == 1) operation = "Write";
            else operation = "X";

            printf("Processo %d: Realizando syscall, entrando em espera...\n", id);
            syscall_sim(device, operation);
            blocked_process[device][id] = 1;
            raise(SIGSTOP);
        }

    }
    printf("Processo %d: Finalizado\n", id);
    exit(0);
}

void create_processes() {
    for (int i = 0; i < NUM_PROCESSES; i++) {
        program_counter[i] = 0;
        processes[i] = fork(); // Cria um processo filho

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
    create_processes(); // Cria os processos de aplicação

    while (1) {
        // Verifica se todos os processos já terminaram
        int all_finished = 1;
        for (int i = 0; i < NUM_PROCESSES; i++) {
            if (program_counter[i] < MAX_ITER) {
                all_finished = 0; // Se algum processo não terminou, setar como não finalizado
                break;
            }
        }

        if (all_finished) {
            printf("Todos os processos finalizaram. Encerrando o kernel.\n");
            break;
        }

        printf("Kernel: Executando processo %d\n", *current_process);
        kill(processes[*current_process], SIGCONT); // Retoma a execução do processo atual
        sleep(TIME_SLICE);

        // Após o time slice, pausa o processo
        kill(processes[*current_process], SIGSTOP); // Pausa o processo
        printf("Kernel: Pausando processo %d\n", *current_process);

        *current_process = (*current_process + 1) % NUM_PROCESSES;
    }
}

void controller_sim() {
    /*
        Emula o controlador de interrupções que gera as interrupções
        referentes ao relógio (IRQ0) e ao término da operação de E/S
        no D1 (IRQ1) e D2 (IRQ2)

        O time slice é de 500 ms
        I/O do D1 é random
        I/O do D2 é random
    */
    while (1) {
        sleep(TIME_SLICE); // IRQ0 a cada 500ms
        kill(*current_process, SIGRTMIN); // Envia IRQ0 (time slice)

        // IRQ1 tem probabilidade de 0.1
        if ((rand() % 100) < 10) {
            kill(*current_process, SIGUSR1); // Envia IRQ1 (D1)
        }

        // IRQ2 tem probabilidade de 0.05
        if ((rand() % 100) < 5) {
            kill(*current_process, SIGUSR2);  // Envia IRQ2 (D2)
        }
    }
}

int main() {
    signal(SIGRTMIN, TS_handler);  // Define handler para SIGRTMIN (time slice)
    signal(SIGUSR1, D1_handler);  // Define handler para SIGUSR1 (D1)
    signal(SIGUSR2, D2_handler); // Define handler para SIGUSR2 (D2)

    int shm_id = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    current_process = (int *)shmat(shm_id, NULL, 0);
    *current_process = 0;  // Inicializa o processo atual

    pid_t ICS_pid = fork();

    if (ICS_pid == 0) {
        controller_sim(); // Inicia a simulação do InterController
    } else {
        pid_t kernel_pid = fork();

        if (kernel_pid == 0) {
            kernel_sim(); // Inicia o kernel de escalonamento
        }
    }

    // Aguarda o término dos processos filhos
    while (wait(NULL) > 0);

    shmdt(current_process);  // Desanexa a memória compartilhada
    shmctl(shm_id, IPC_RMID, NULL);  // Remove o segmento de memória compartilhada

    return 0;
}
