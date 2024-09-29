#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#define NUM_PROCESSES 3     // num de processos
#define MAX_ITER 10        // maximo de interacoes dos procesos
#define TIME_SLICE 0.5    // 500ms

pid_t processes[NUM_PROCESSES];      // array de Pids
int program_counter[NUM_PROCESSES]; // contador cada processo
int current_process = 0;           // processo em execucao
int pipes[2];                     // descritor de pipes
int process_blocked[NUM_PROCESSES];
int device_blocked[2];

void sigcont_handler(int signum) {
    printf("Processo %d: Retomando execução\n", getpid());
}

void sigstop_handler(int signum) {
    printf("Processo %d: Pausado pelo Kernel\n", getpid());
    pause();  // processo esperando retomada 
}

void syscall_sim(int device, int process_id, char operation) {
    if (operation == 'R') {
        char result;
        read(pipes[0], result, sizeof(char));
    }
    else if (operation == 'W') {
        write(pipes[1], '0', sizeof(char));
    }
    else {
        //nem sei oq eh X
    }
    process_blocked[process_id] = 1; // Bloqueia o processo
    device_blocked[process_id] = device;
    printf("Processo %d bloqueado no dispositivo D%d\n", process_id, device);
}

void process_application(int id) {
    int device, operation;
    int chance;

    while (program_counter[id] < MAX_ITER) {
        printf("Processo %d: Executando, PC=%d\n", id, program_counter[id]);
        program_counter[id]++;
        sleep(1); // Simulação de trabalho (100ms por iteração)

        if (chance == rand() % 100 < 15) {  // Simula chance de fazer uma syscall (15% de chance)
            if (chance % 2) device = 1;
            else device = 2;
            if (chance % 3 == 1) operation = 'R';
            else if (chance % 3 == 1) operation = 'W';
            else operation = 'X';
            syscall_sim(device, id, operation);
            
            printf("Processo %d: Realizando syscall, entrando em espera...\n", id);
            kill(getpid(), SIGSTOP);
            pause(); // Simula bloqueio por syscall
        }
    }

    printf("Processo %d: Finalizado\n", id);
    exit(0);
}

void create_processes() {
    signal(SIGCONT, sigcont_handler); // Define o handler para SIGCONT
    signal(SIGSTOP, sigstop_handler); // Define o handler para SIGSTOP

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
    create_processes(); // Cria os processos de aplicação 

    while(1) {
        printf("Kernel: Executando processo %d\n", current_process);
        kill(processes[current_process], SIGCONT); // Continua o processo atual

        sleep(TIME_SLICE); // Espera o time slice (500ms)

        // Pausa o processo atual e seleciona o próximo
        kill(processes[current_process], SIGSTOP); // Pausa o processo
        current_process = (current_process + 1) % NUM_PROCESSES; // Seleciona o próximo processo
    }
}

void controller_sim(){
    while(1){
        
    }
}

int main() {
    if (pipe(pipes) < 0) {
        puts("error creating pipe");
        exit(1);
    }

    pid_t kernel_pid = fork(); // criando o processo do InterController

    if (kernel_pid == 0) {
        kernel_sim();  // Inicia a simulação do Kernel
    }
    else {
        pid_t ICS_pid = fork();

        if (ICS_pid == 0) {
            controller_sim();
        }
    }

    // srand(time(NULL));   // Inicializa o gerador de números aleatórios

    return 0;
}