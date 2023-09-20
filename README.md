## Alarme para portas, portão, garagem, etc...

- Projeto realizado no curso de extensão "RTOS para microcontroladores" ofertado pela Universidade Federal do Piauí - UFPI.

- Projeto feito utilizando MQTT(Broker do TagoIO) e FreeRTOS com 2 ESP32.

- Adapte GPIOs, credenciais da rede WIFI e o Token do Broker MQTT de acordo com sua necessidade.


## Funcionalidades Principais

- Quando a porta for aberta (ao pressionar o botão), começa a contar o tempo possível para inserir a senha correta.
	- Caso o tempo estoure e a senha correta não for inserida, o alarme dispara (LED fica piscando).
	- Caso a senha correta seja inserida, o contador para e o alarme desarma (caso tenha sido disparado).
- Utilizando um sensor de luminosidade, ao receber pouca iluminação (interpretado como noite) as câmeras são ligadas (LEDs).
- A senha se baseia nos botões, um botão representa o número 1 e o outro botão representa o número 2.


## Autores

- [Franklin William Silva Santos](mailto:franklin.santos@ufpi.edu.br)  - Engenharia Elétrica (UFPI) 
- [João Filipe Batista e Silva](mailto:joaofilipe023@ufpi.edu.br) - Ciência da Computação (UFPI)
