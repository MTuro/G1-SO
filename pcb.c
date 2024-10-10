#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>

/*
- verificar se o syscall está bloqueando corretamente e fazer a parte do "Time-sharing" do pdf
- fila de processos bloqueados para cada um dos dispositivos D1 e D2
- ver melhor a parte de "Visualizando os estados dos processos" no pdf
*/

#define MAX_PROCESSES 3
#define MAX_PC 10
#define TIME_SLICE_ALARM 3
#define QUEUE_SIZE MAX_PROCESSES

// Estrutura PCB (Process Control Block)
typedef struct {
    int pid;            // PID do processo
    int pc;             // Program counter
    int state;          // Estado do processo: 0 = pronto, 1 = executando, 2 = bloqueado, 3 = finalizado
    int waiting_device; // 0 = nenhum, 1 = D1, 2 = D2
} PCB;

PCB *pcbs;  // Array para armazenar os PCBs
int *current_process = 0; // Processo que está executando

// Pipes para comunicação entre kernel_sim e inter_controller_sim
int pipefd[2];

int device1_fila[QUEUE_SIZE], device2_fila[QUEUE_SIZE];
int d1_topo = 0, d1_fim = 0, d2_topo = 0, d2_fim = 0;

void insere_fila(int *fila, int *fim, int pid) {
    fila [*fim] = pid;
    *fim = (*fim + 1) % QUEUE_SIZE;
}

int remove_fila(int *fila, int *cabeca) {
    int pid = fila[*cabeca];
    *cabeca = (*cabeca + 1) % QUEUE_SIZE;
    return pid;
}

// Simulação de syscall
void sigrtmin_handler(int sig) {
    printf("Processo %d: realizando chamada de sistema (syscall)\n", *current_process);
    pcbs[*current_process].state = 2; // Processo bloqueado
    pcbs[*current_process].waiting_device = rand() % 2 + 1; // Dispositivo aleatório D1 ou D2

    if(pcbs[*current_process].waiting_device == 1){
        insere_fila(device1_fila, &d1_fim, *current_process);
    }
    else{
        insere_fila(device2_fila, &d2_fim, *current_process);
    }

    raise(SIGSTOP); // Para o processo
}

void irq_handler(int sig) {
    if (sig == SIGALRM) {
        printf("IRQ0 (Time Slice): processo %d interrompido\n", *current_process);

        if (pcbs[*current_process].state == 2) {
            printf("Processo %d está bloqueado aguardando o dispositivo %d\n", *current_process, pcbs[*current_process].waiting_device);
        } else if (pcbs[*current_process].state == 1){
            kill(pcbs[*current_process].pid, SIGSTOP);
            pcbs[*current_process].state = 0;
        }

        do {
            *current_process = (*current_process + 1) % MAX_PROCESSES;
        } while (pcbs[*current_process].state == 3 || pcbs[*current_process].state == 2);

        if (pcbs[*current_process].state != 3) {
            kill(pcbs[*current_process].pid, SIGCONT);
            pcbs[*current_process].state = 1;
        }
    } 
    else if (sig == SIGUSR1) {
        // Desbloquear processo esperando pelo dispositivo 1 (D1)
        printf("o q tem nos d1: %d--%d\n",d1_topo,d1_fim);
        if (device1_fila) {
            int pid = remove_fila(device1_fila, &d1_topo);
            printf("Processo %d desbloqueado após E/S (Dispositivo 1)\n", pid);
            pcbs[pid].state = 0;
            pcbs[pid].waiting_device = 0;
            kill(pcbs[pid].pid, SIGCONT);  // Libera o processo
        }
    }
    else if (sig == SIGUSR2) {
        // Desbloquear processo esperando pelo dispositivo 2 (D2)
        printf("o q tem nos d2: %d--%d\n",d2_topo,d2_fim);
        if (device2_fila) {
            int pid = remove_fila(device2_fila, &d2_topo);
            printf("Processo %d desbloqueado após E/S (Dispositivo 2)\n", pid);
            pcbs[pid].state = 0;
            pcbs[pid].waiting_device = 0;
            kill(pcbs[pid].pid, SIGCONT);  // Libera o processo
        }
    }

}


void create_processes() {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        int pid = fork();
        if (pid == 0) {
            signal(SIGRTMIN, sigrtmin_handler);  // Configura syscall handler para processo filho
            signal(SIGCONT, SIG_DFL);  // Habilita continuar com SIGCONT

            // Simulação de execução do processo
            for (pcbs[i].pc = 0; pcbs[i].pc < MAX_PC; pcbs[i].pc++) {
                pcbs[i].state = 1;
                printf("Processo %d: executando, PC=%d\n", i, pcbs[i].pc);
                sleep(1);  // Simula tempo de execução
                int x;
                srand(time(NULL));
                if (( x = (rand() % 100)) < 15) {  // Simula uma syscall aleatória
                    printf("%d\n", x);
                    raise(SIGRTMIN);  // Envia o sinal de syscall
                }
            }
            pcbs[i].state = 3; // Estado finalizado
            printf("Processo %d: Finalizado\n", i);
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
    signal(SIGUSR1, irq_handler);  // Dispositivo D1 (IRQ1)
    signal(SIGUSR2, irq_handler);  // Dispositivo D2 (IRQ2)

    create_processes();

    kill(pcbs[0].pid, SIGCONT);
    kill(pcbs[1].pid, SIGSTOP);
    kill(pcbs[2].pid, SIGSTOP);
    pcbs[0].state = 1;

    char buffer[2];
    while (1) {
        read(pipefd[0], buffer, 2);  // Lê a interrupção enviada por inter_controller_sim

        if (buffer[0] == 'T') {  // Timer IRQ (Time Slice)
            raise(SIGALRM);
        } else if (buffer[0] == 'I' && buffer[1] == '1') {  // Interrupção de E/S (Dispositivo D1)
            printf("recebendo IRQ1\n");
            raise(SIGUSR1);
        } else if (buffer[0] == 'I' && buffer[1] == '2') {  // Interrupção de E/S (Dispositivo D2)
            printf("recebendo IRQ2\n");
            raise(SIGUSR2);
        }
    }
}


void inter_controller_sim() {
    while (1) {
        sleep(TIME_SLICE_ALARM);  // 500 ms = 0.5 segundos

        // Envia IRQ0 (Time Slice)
        write(pipefd[1], "T", 1);

        srand(time(NULL));

        // Gera IRQ1 com probabilidade P_1 = 0.1 (10%)
        if ((rand() % 100) < 100) {
            printf("enviando IRQ1\n");
            write(pipefd[1], "I1", 2);  // Envia interrupção de E/S para dispositivo D1
        }

        // Gera IRQ2 com probabilidade P_2 = 0.05 (5%)
        if ((rand() % 100) < 100) {
            write(pipefd[1], "I2", 2);  // Envia interrupção de E/S para dispositivo D2
        }
    }
}


int main() {
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    int shm_id = shmget(1111, sizeof(int), IPC_CREAT | 0666);
    current_process = (int *)shmat(shm_id, NULL, 0);
    *current_process = 0;  // Inicializa o processo atual

    // int shm_d1fila = shmget(1112, sizeof(device1_fila), IPC_CREAT | 0666);
    // device1_fila = (int *)shmat(shm_d1fila, NULL, 0);  // Associa a fila do device 1

    // int shm_d2fila = shmget(1112, sizeof(device2_fila), IPC_CREAT | 0666);
    // device2_fila = (int *)shmat(shm_d2fila, NULL, 0);  // Associa a fila do device 1

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

    shmdt(pcbs);  // Desanexa a memória compartilhada
    shmctl(shm_pcb, IPC_RMID, NULL);  // Libera a memória

    return 0;
}
