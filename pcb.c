#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <sys/stat.h>

/*
FUNCIONOUUUU, agora:
- fazer a parte do "Time-sharing" do pdf
- ver melhor a parte de "Visualizando os estados dos processos" no pdf
- arrumar os prints (ta printando sempre "processo i bloquado/interrompido" 
quando ta esperando alugm processo ser desbloqueado)
- ver pq nao funciona com mais de 3 processos
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

int *device1_fila, *device2_fila;
int d1_topo, d2_topo;

void insere_fila(int *fila, int pid) {
    for (int i = 0; i < QUEUE_SIZE; i++) {
        if (fila[i] == -1) {  // Encontra o primeiro espaço vazio (-1)
            fila[i] = pid;  // Adiciona o processo na posição encontrada
            return;
        }
    }
}

int remove_fila(int *fila) {
    int topo = fila[0];
    fila[0] = -1;

    // Reorganiza a fila para mover os espaços vazios (-1) para o final
    for (int i = 0; i < QUEUE_SIZE- 1; i++) {
        if (fila[i] == -1) {
            for (int j = i + 1; j < QUEUE_SIZE; j++) {
                if (fila[j] != -1) {
                    fila[i] = fila[j];
                    fila[j] = -1;
                    break;
                }
            }
        }
    }

    return topo;
}

// Simulação de syscall
void sigrtmin_handler(int sig) {
    printf("Processo %d: realizando chamada de sistema (syscall)\n", *current_process);
    pcbs[*current_process].state = 2; // Processo bloqueado
    pcbs[*current_process].waiting_device = rand() % 2 + 1; // Dispositivo aleatório D1 ou D2

    if(pcbs[*current_process].waiting_device == 1){
        insere_fila(device1_fila, *current_process);
    }
    else{
        insere_fila(device2_fila, *current_process);
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

        int processos_parados = 0;

        *current_process = (*current_process + 1) % MAX_PROCESSES;
               
        while (pcbs[*current_process].state == 3 || pcbs[*current_process].state == 2){
            processos_parados++;
            if (processos_parados == 3){
                break;
            }

            *current_process = (*current_process + 1) % MAX_PROCESSES;
        }
        

        if (pcbs[*current_process].state == 0) {
            kill(pcbs[*current_process].pid, SIGCONT);
            pcbs[*current_process].state = 1;
        }

    } 
    else if (sig == SIGUSR1) {
        // Desbloquear processo esperando pelo dispositivo 1 (D1)
        if (device1_fila[0] != -1) {
            int pid = remove_fila(device1_fila);
            printf("Processo %d desbloqueado após E/S (Dispositivo 1)\n", pid);
            pcbs[pid].state = 0;
            pcbs[pid].waiting_device = 0;
        }
    }
    else if (sig == SIGUSR2) {
        // Desbloquear processo esperando pelo dispositivo 2 (D2)
        if (device2_fila[0] != -1) {
            int pid = remove_fila(device2_fila);
            printf("Processo %d desbloqueado após E/S (Dispositivo 2)\n", pid);
            pcbs[pid].state = 0;
            pcbs[pid].waiting_device = 0;
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
                srand(time(NULL));
                if ((rand() % 100) < 15) {  // Simula uma syscall aleatória
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
    for (int i = 1; i< MAX_PROCESSES; i++){
        kill(pcbs[i].pid, SIGSTOP);
    }
    pcbs[0].state = 1;

    char buffer[1];
    while (1) {
        read(pipefd[0], buffer, 1);  // Lê a interrupção enviada por inter_controller_sim

        if (buffer[0] == 'T') {  // Timer IRQ (Time Slice)
            raise(SIGALRM);
        } else if (buffer[0] == '1') {  // Interrupção de E/S (Dispositivo D1)
            printf("recebendo IRQ1\n");
            raise(SIGUSR1);
        } else if (buffer[0] == '2') {  // Interrupção de E/S (Dispositivo D2)
            printf("recebendo IRQ2\n");
            raise(SIGUSR2);
        }
        int processos_finalizados = 0;
        for (int i = 0; i < MAX_PROCESSES; i++){
            if (pcbs[i].state == 3)
                processos_finalizados++;
            
        }

        if (processos_finalizados == 3){
            printf("Todos os processos foram finalizados\n");
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
        if ((rand() % 100) < 10) {
            printf("enviando IRQ1\n");
            write(pipefd[1], "1", 1);  // Envia interrupção de E/S para dispositivo D1
        }

        // Gera IRQ2 com probabilidade P_2 = 0.05 (5%)
        if ((rand() % 100) < 5) {
            printf("enviando IRQ2\n");
            write(pipefd[1], "2", 1);  // Envia interrupção de E/S para dispositivo D2
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

    int shm_pcbs = shmget(1112, sizeof(PCB) * MAX_PROCESSES, IPC_CREAT | 0666);
    pcbs = (PCB *)shmat(shm_pcbs, NULL, 0);

    int shm_device1_fila = shmget(1114, sizeof(int) * QUEUE_SIZE, IPC_CREAT | 0666);
    device1_fila = (int *)shmat(shm_device1_fila, NULL, 0);

    // Criar segmento de memória compartilhada para device2_fila
    int shm_device2_fila = shmget(1115, sizeof(int) * QUEUE_SIZE, IPC_CREAT | 0666);
    device2_fila = (int *)shmat(shm_device2_fila, NULL, 0);

    for(int i = 0; i<QUEUE_SIZE; i++){
        device1_fila[i] = -1;
        device2_fila[i] = -1;
    }

    int pid = fork();
    if (pid == 0) {
        // Processo pai: kernel_sim
        close(pipefd[1]);  // Fecha o lado de escrita do pipe
        kernel_sim();
    } else {
        
        // Processo filho: inter_controller_sim
        close(pipefd[0]);  // Fecha o lado de leitura do pipe
        inter_controller_sim();
    }

    shmdt(current_process);  // Desanexa a memória compartilhada
    shmctl(shm_id, IPC_RMID, NULL);  // Remove o segmento de memória compartilhada

    shmdt(pcbs);
    shmctl(shm_pcbs, IPC_RMID, NULL);

    // Desanexa as filas de memória compartilhada
    shmdt(device1_fila);
    shmdt(device2_fila);

    // Remove os segmentos de memória compartilhada das filas
    shmctl(shm_device1_fila, IPC_RMID, NULL);
    shmctl(shm_device2_fila, IPC_RMID, NULL);

    return 0;
}
