Projeto realizado no curso de extensão "RTOS para microcontroladores" ofertado pela Universidade Federal do Piauí - UFPI.
Projeto feito utilizando MQTT(Broker do TagoIO) e FreeRTOS com 2 ESP32.
Adapte GPIOs, credenciais da rede WIFI e o Token do Broker MQTT de acordo com sua necessidade.

Funcionamento:
	- Quando a porta for aberta (ao pressionar o botão), começa a contar o tempo possível para inserir a senha correta.
	- Caso o tempo estoure e a senha correta não for inserida, o alarme dispara (LED fica piscando).
	- Caso a senha correta seja inserida, o contador para e o alarme desarma (caso tenha sido disparado).
	- Utilizando um sensor de luminosidade, ao receber pouca iluminação (interpretado como noite) as câmeras são ligadas (LEDs).
	- A senha se baseia nos botões, um botão representa o número 1 e o outro botão representa o número 2.

Autores:
	- Franklin William Silva Santos - Engenharia Elétrica (UFPI): franklin.santos@ufpi.edu.br
	- João Filipe Batista e Silva - Ciência da Computação (UFPI): joaofilipe023@ufpi.edu.br
