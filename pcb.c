#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define MAX_PROCESSES 3
#define MAX_PC 50
#define TIME_SLICE_ALARM 1

// Estrutura PCB (Process Control Block)
typedef struct {
    int pid;            // PID do processo
    int pc;             // Program counter
    int state;          // Estado do processo: 0 = pronto, 1 = executando, 2 = bloqueado, 3 = finalizado
    int waiting_device; // 0 = nenhum, 1 = D1, 2 = D2
} PCB;

PCB pcbs[MAX_PROCESSES];  // Array para armazenar os PCBs
int *current_process = 0; // Processo que está executando

// Pipes para comunicação entre kernel_sim e inter_controller_sim
int pipefd[2];

// Simulação de syscall
void sigusr1_handler(int sig) {
    printf("Processo %d: realizando chamada de sistema (syscall)\n", *current_process);
    pcbs[*current_process].state = 2; // Processo bloqueado
    pcbs[*current_process].waiting_device = rand() % 2 + 1; // Dispositivo aleatório D1 ou D2
    raise(SIGSTOP); // Para o processo
}

void irq_handler(int sig) {
    if (sig == SIGALRM) {
        printf("IRQ0 (Time Slice): processo %d interrompido\n", *current_process);

        // Se o processo atual estiver bloqueado, imprime a mensagem e passa para o próximo
        if (pcbs[*current_process].state == 2) {
            printf("Processo %d está bloqueado aguardando o dispositivo %d\n", *current_process, pcbs[*current_process].waiting_device);
        } else {
            kill(pcbs[*current_process].pid, SIGSTOP); // Interrompe o processo atual
        }

        // Alterna para o próximo processo
        do {
            *current_process = (*current_process + 1) % MAX_PROCESSES;  // Alterna para o próximo processo
        } while (pcbs[*current_process].state == 3 || pcbs[*current_process].state == 2); // Pula processos finalizados ou bloqueados

        // Retoma o próximo processo se não estiver bloqueado ou finalizado
        if (pcbs[*current_process].state != 3) {
            kill(pcbs[*current_process].pid, SIGCONT);  // Retoma o próximo processo
            pcbs[*current_process].state = 1; // Define como executando
        }
    } else if (sig == SIGUSR2) {
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (pcbs[i].state == 2) {
                printf("Processo %d desbloqueado após E/S (Dispositivo %d)\n", i, pcbs[i].waiting_device);
                pcbs[i].state = 0;  // Processo desbloqueado
                pcbs[i].waiting_device = 0;  // Nenhum dispositivo mais aguardado
            }
        }
    }
}

void create_processes() {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        int pid = fork();
        if (pid == 0) {
            signal(SIGRTMIN, sigusr1_handler);  // Configura syscall handler para processo filho
            signal(SIGCONT, SIG_DFL);  // Habilita continuar com SIGCONT

            // Simulação de execução do processo
            for (pcbs[i].pc = 0; pcbs[i].pc < MAX_PC; pcbs[i].pc++) {
                printf("Processo %d: executando, PC=%d\n", i, pcbs[i].pc);
                sleep(1);  // Simula tempo de execução
                if (rand() % 100 < 15) {  // Simula uma syscall aleatória
                    raise(SIGRTMIN);  // Envia o sinal de syscall
                }
            }

            printf("Processo %d: Finalizado\n", i);
            pcbs[i].state = 3; // Estado finalizado
            exit(0);
        } else {
            // Preenche o PCB do processo pai
            pcbs[i].pid = pid;
            pcbs[i].pc = 0;
            pcbs[i].state = 0; // Pronto para execução
        }
    }
}

void kernel_sim() {
    signal(SIGALRM, irq_handler);  // Timer (Time Slice)
    signal(SIGUSR1, irq_handler);  // Dispositivos de E/S (IRQ1)
    signal(SIGUSR2, irq_handler);  // Dispositivos de E/S (IRQ2)

    create_processes();

    // Retoma o primeiro processo
    kill(pcbs[0].pid, SIGCONT);
    kill(pcbs[1].pid, SIGSTOP);
    kill(pcbs[2].pid, SIGSTOP);
    pcbs[0].state = 1;  // Executando

    char buffer;
    while (1) {
        read(pipefd[0], &buffer, 1);  // Lê a interrupção enviada por inter_controller_sim

        if (buffer == 'T') {  // Timer IRQ (Time Slice)
            raise(SIGALRM);
        } else if (buffer == 'I') {  // Interrupção de E/S
            raise(SIGUSR1);
        } 
    }
}

void inter_controller_sim() {
    srand(time(NULL));

    while (1) {
        sleep(TIME_SLICE_ALARM);  // Simula o time slice a cada 1 segundo
        write(pipefd[1], "T", 1);  // Envia interrupção de timer para o kernel_sim

        // Simula interrupções de E/S com maior probabilidade
        int chance = rand() % 100;
        if (chance < 10) {  // Aumente a probabilidade para 20%
            write(pipefd[1], "I", 2);  // Envia interrupção de E/S para o kernel_sim
        }
    }
}

int main() {
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    int shm_id = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    current_process = (int *)shmat(shm_id, NULL, 0);
    *current_process = 0;  // Inicializa o processo atual

    int pid = fork();
    if (pid == 0) {
        // Processo filho: inter_controller_sim
        close(pipefd[0]);  // Fecha o lado de leitura do pipe
        inter_controller_sim();
    } else {
        // Processo pai: kernel_sim
        close(pipefd[1]);  // Fecha o lado de escrita do pipe
        kernel_sim();
    }

    shmdt(current_process);  // Desanexa a memória compartilhada
    shmctl(shm_id, IPC_RMID, NULL);  // Remove o segmento de memória compartilhada

    return 0;
}
