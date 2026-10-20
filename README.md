# Soccer SSL — primeiro teste de visão

Este primeiro sketch testa a câmera da placa Romeo ESP32-S3 e a detecção visual de cores para o projeto do robô Soccer.

## O que o teste faz

- Captura e exibe a imagem da câmera em **320 × 240**.
- Espelha a imagem horizontalmente para que o movimento para a direita apareça para a direita na página.
- Classifica a cor predominante do quadro entre preto, cinza, branco, vermelho, laranja, amarelo, verde, ciano, azul, roxo e rosa.
- Procura as maiores regiões conectadas vermelha e laranja e desenha uma caixa em cada região encontrada.
- Mostra a quantidade de amostras detectadas para vermelho e laranja e as coordenadas do centro da região laranja.

A detecção é baseada na cor; nesta etapa, ela ainda não identifica o formato da bola nem controla os motores. A iluminação e o material do objeto podem afetar a classificação.

## Como carregar

1. Abra `Soccer_SSL.ino` no Arduino IDE.
2. Selecione a placa **DFRobot Romeo ESP32-S3** (DFR0994) e habilite **USB CDC On Boot**.
3. Grave o sketch na placa. O Monitor Serial em **115200 baud** mostra o início da câmera e os dados da rede.

## Acessar a câmera e os resultados

O próprio ESP32 cria uma rede Wi-Fi. Conecte o computador ou celular a ela e abra o endereço abaixo no navegador:

- **Rede (SSID):** `Romeo-Camera`
- **Senha:** `SoccerCam1`
- **Site:** [http://192.168.4.1/](http://192.168.4.1/)

Não há login separado no site: SSID e senha são as credenciais da rede Wi-Fi. A página mostra simultaneamente o vídeo, a cor predominante, os avisos de vermelho/laranja e as caixas de localização.

## Arquivo

- `Soccer_SSL.ino` — sketch Arduino com câmera, servidor web e análise de cores.
