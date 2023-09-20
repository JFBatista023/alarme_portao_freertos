// Inclusão de bibliotecas
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/message_buffer.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "driver/gpio.h"
#include "cJSON.h"

#define WIFI_SSID "Nome da sua rede"
#define WIFI_PASSWORD "Senha da sua rede"
#define MQTT_TOKEN "Token do seu Broker MQTT"
#define MQTT_ID_CLIENT "Alarme_MQTT"

#define LED_BRANCO 21
#define LED_VERMELHO 22
#define LED_AZUL 23
#define BUTTON 19

#define MESSAGE_BUFFER_SIZE 100
#define TAG "MQTT"

TaskHandle_t xtask_button_handler = NULL, xtask_leds_handler = NULL;
QueueHandle_t xqueue_button;
MessageBufferHandle_t xmsg_buffer;
bool cameras_ligadas = false, led_ligado = false;

void vtask_leds(void *parameters)
{
    uint8_t led_status = 0;
    while (1)
    {
        led_status = !led_status;
        gpio_set_level(LED_VERMELHO, led_status);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void vtask_porta(void *pvParameters)
{
    uint32_t counter_btn = 0;
    while (1)
    {
        if (xMessageBufferReceive(xmsg_buffer, (void *)&counter_btn, sizeof(xmsg_buffer), portMAX_DELAY) > 0)
        {
            ESP_LOGI(TAG, "A porta já foi aberta %ldx).", counter_btn);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Declarando o handler do client global
static esp_mqtt_client_handle_t client = NULL;

static esp_err_t mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch (event_id)
    {
    case MQTT_EVENT_CONNECTED:

        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "tago/data/post", 0);
        ESP_LOGI(TAG, "Envio do subscribe com sucesso, msg_id=%d", msg_id);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "publish enviado com sucesso, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        // Parse o JSON
        cJSON *root = cJSON_Parse(event->data);

        if (root == NULL)
        {
            printf("Erro ao analisar JSON.\n");
            return 1;
        }

        // Verifica se o JSON é uma matriz
        if (cJSON_IsArray(root))
        {
            // Obtém o primeiro elemento da matriz
            cJSON *element = cJSON_GetArrayItem(root, 0);

            // Verifica se o elemento é um objeto JSON
            if (cJSON_IsObject(element))
            {
                // Obtém os valores das chaves "variable" e "value"
                cJSON *variable = cJSON_GetObjectItem(element, "variable");
                cJSON *value = cJSON_GetObjectItem(element, "value");

                // Verifica se as chaves existem e são do tipo string e número
                if (cJSON_IsString(variable) && cJSON_IsNumber(value))
                {
                }
                else if (cJSON_IsString(variable) && cJSON_IsString(value))
                {
                    if (strcmp(variable->valuestring, "tempo") == 0 && strcmp(value->valuestring, "esgotado") == 0)
                    {
                        ESP_LOGE(TAG, "Tempo para desarmar o alarme esgotado!");
                        ESP_LOGE(TAG, "Alarme ativado!");
                        if (!led_ligado)
                        {
                            led_ligado = true;
                            xTaskCreate(vtask_leds, "Task LEDS", 1024 * 5, NULL, 1, &xtask_leds_handler);
                        }
                    }
                    else if (strcmp(variable->valuestring, "porta") == 0 && strcmp(value->valuestring, "aberta") == 0)
                    {
                        uint32_t counter_btn = 0;
                        if (xQueueReceive(xqueue_button, &counter_btn, portMAX_DELAY) == pdTRUE)
                        {
                            ESP_LOGW(TAG, "A porta está aberta!");
                            ESP_LOGW(TAG, "Esperando confirmação de senha.");
                            xMessageBufferSend(xmsg_buffer, (void *)&counter_btn, sizeof(counter_btn), 0);
                            gpio_set_level(LED_BRANCO, 1);
                            vTaskSuspend(xtask_button_handler);
                        }
                    }
                    else if (strcmp(variable->valuestring, "porta") == 0 && strcmp(value->valuestring, "fechada") == 0)
                    {
                        ESP_LOGW(TAG, "A porta está fechada!");
                        gpio_set_level(LED_BRANCO, 0);
                        vTaskResume(xtask_button_handler);
                    }
                    else if (strcmp(variable->valuestring, "senha") == 0 && strcmp(value->valuestring, "correta") == 0)
                    {
                        ESP_LOGW(TAG, "Senha correta!");
                        if (led_ligado)
                        {
                            led_ligado = false;
                            vTaskDelete(xtask_leds_handler);
                            gpio_set_level(LED_VERMELHO, 0);
                            ESP_LOGE(TAG, "Alarme desativado!");
                        }
                        char msg[50];                                                             // ler a temperatura do sensor
                        sprintf(msg, "[{\"variable\": \"porta\",\"value\": \"%s\"}]", "fechada"); // Cria uma mensagem fake para publicar no broken
                        esp_mqtt_client_publish(client, "tago/data/post", msg, 0, 1, 0);
                    }
                    else if (strcmp(variable->valuestring, "cameras") == 0 && strcmp(value->valuestring, "ligadas") == 0)
                    {
                        ESP_LOGI(TAG, "As câmeras foram ligadas!");
                        cameras_ligadas = true;
                    }
                    else if (strcmp(variable->valuestring, "cameras") == 0 && strcmp(value->valuestring, "desligadas") == 0)
                    {
                        ESP_LOGI(TAG, "As câmeras foram desligadas!");
                        cameras_ligadas = false;
                    }
                }
                else
                {
                    printf("Chaves inválidas no objeto JSON.\n");
                }
            }
            else
            {
                printf("Elemento não é um objeto JSON.\n");
            }
        }
        else
        {
            printf("JSON não é uma matriz.\n");
        }

        // Libera a memória alocada pela cJSON
        cJSON_Delete(root);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    }
    return ESP_OK;
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:

        ESP_LOGI(TAG, "Conectando ao roteador");
        break;

    case WIFI_EVENT_STA_DISCONNECTED:

        ESP_LOGE(TAG, "Falha ao conectar ao Ponto de Acesso");
        break;

    case IP_EVENT_STA_GOT_IP:

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP:" IPSTR, IP2STR(&event->ip_info.ip));
        break;
    }
}
// Configurando o driver do WiFi
static void wifi_init(void)
{

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id; // Handler para indentificar o contexto do evento
    esp_event_handler_instance_t instance_got_ip; // Handler para indentificar o contexto do evento

    // Registra as instâncias
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    wifi_config_t wifi_config =
        {
            .sta =
                {
                    .ssid = WIFI_SSID,
                    .password = WIFI_PASSWORD,
                },
        };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));                   // Configura o driver para moodo Station
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config)); // Passa a configuração de SSID e senha

    ESP_LOGI(TAG, "SSID:[%s] Senha:[%s]", WIFI_SSID, WIFI_SSID);

    ESP_ERROR_CHECK(esp_wifi_start());   // Inicia o wifi
    ESP_ERROR_CHECK(esp_wifi_connect()); // Perfoma a conexão com a rede
    ESP_LOGI(TAG, "Aguardando conectar ...");
}

static void mqtt_app_start(void)
{

    ESP_LOGI(TAG, "Inicializando modulo MQTT...");

    /* Realiza a conexão ao broker MQTT */

    esp_mqtt_client_config_t config_mqtt =
        {
            .broker.address.uri = "mqtt://mqtt.tago.io:1883",  // URI do Tago IO
            .credentials.authentication.password = MQTT_TOKEN, // Token do device
            .credentials.username = "tago",                    // Nome de usário
            .credentials.client_id = MQTT_ID_CLIENT            // Nome do seu device
        };

    // Cria um handler para o client
    client = esp_mqtt_client_init(&config_mqtt);
    // Registra a função de callback para o handler do client
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);

    // Inicializa o client, autenticação no broken
    if (esp_mqtt_client_start(client) != ESP_OK)
    {
        ESP_LOGW(TAG, "MQTT não inicializado. Confira a URI ou as credênciais");
        return;
    }

    ESP_LOGI(TAG, "MQTT inicializado");
}

void vtask_button(void *parameters)
{
    bool btn_is_press = false;
    uint32_t counter_press = 0;

    while (1)
    {
        if (!gpio_get_level(BUTTON) && !btn_is_press)
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            btn_is_press = true;
            counter_press++;
            xQueueSend(xqueue_button, &counter_press, 0);

            char msg[50];                                                            // ler a temperatura do sensor
            sprintf(msg, "[{\"variable\": \"porta\",\"value\": \"%s\"}]", "aberta"); // Cria uma mensagem fake para publicar no broken
            esp_mqtt_client_publish(client, "tago/data/post", msg, 0, 1, 0);         // Client envia o publish para o Broken
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
        else if (gpio_get_level(BUTTON) && btn_is_press)
        {
            btn_is_press = false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void vtask_cameras(void *parameters)
{
    while (1)
    {
        if (cameras_ligadas)
        {
            gpio_set_level(LED_AZUL, 1);
        }
        else
        {
            gpio_set_level(LED_AZUL, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void init_gpios()
{
    gpio_config_t button_config = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .pin_bit_mask = 1 << BUTTON};

    gpio_config(&button_config);

    gpio_set_direction(LED_BRANCO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_AZUL, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_VERMELHO, GPIO_MODE_OUTPUT);
}

void app_main()
{
    init_gpios();
    nvs_flash_init(); // Inicializando o NVS
    wifi_init();      // Inicializando WiFi

    vTaskDelay(pdMS_TO_TICKS(10000)); // Aguarda 10s até estabilizar a conexão com a rede
    mqtt_app_start();                 // Cria o novo client e conecta-se ao broke

    xmsg_buffer = xMessageBufferCreate(MESSAGE_BUFFER_SIZE);
    xqueue_button = xQueueCreate(20, sizeof(uint32_t));
    if (xqueue_button == NULL)
    {
        ESP_LOGE(TAG, "Erro ao alocar a fila");
        return;
    }

    xTaskCreate(vtask_button, "Task Button", 1024 * 5, NULL, 1, &xtask_button_handler);
    xTaskCreate(vtask_cameras, "Task Cameras", 1024 * 5, NULL, 1, NULL);
    xTaskCreate(vtask_porta, "Task Porta", 1024 * 5, NULL, 1, NULL);
}