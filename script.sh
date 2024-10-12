#Ana Luiza Pinto Marques - 2211960
#Marcos Turo Ferandes Junior - 2211712

#!/bin/bash

#para rodar :
#chmod 755 script.sh
#./script.sh

# Compilar o código
echo "Compilando o código..."
gcc -o pcb pcb.c  # Substitua pelo nome do arquivo

# Executar o código em segundo plano com job control
echo "Executando o programa..."
./pcb &  # Executa em segundo plano e cria um job controlado pelo shell
PID=$!  # Pega o PID do processo que acabou de rodar

echo "Processo executando com PID $PID"

# Aguarda até que o usuário queira pausar 
read -p $'\nPressione Enter para pausar o processo\n'

# Pausar o processo
echo "Pausando o processo com PID $PID..."
kill -STOP $PID

# Aguarda até que o usuário queira continuar
read -p $'\nPressione Enter para retomar o processo\n\n'

# Retomar o processo
echo "Retomando o processo..."
kill -CONT $PID

# Traz o processo para o foreground manualmente sem job control
wait $PID  # O wait vai aguardar o processo terminar no foreground e permite Ctrl+C

echo "Processo $PID finalizado ou interrompido manualmente."
