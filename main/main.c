#include <stdio.h>
 #include <stdbool.h>
 #include <inttypes.h> // Para PRId32
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "freertos/semphr.h" // Para Mutex
 #include "freertos/timers.h" // Para Software Timers
 #include "driver/gpio.h"
 #include "esp_log.h"
 #include "sdkconfig.h" // Para leer la configuración de menuconfig

 // Incluir las cabeceras de las librerías
 #include "ili9341.h"
 #include "digitos.h" // Asume que este archivo existe y define Panel_t, CrearPanel, DibujarDigito, etc.

 // Parámetros de dibujo de dígitos
 #define DIGITO_ANCHO     60
 #define DIGITO_ALTO      100
 #define DIGITO_ENCENDIDO ILI9341_RED
 #define DIGITO_APAGADO   0x1800
 #define DIGITO_FONDO     ILI9341_BLACK

 // Definición de offset en mayúsculas
 #define OFFSET_X 10
 
 // Coordenadas X, Y y dimensiones para los paneles y separadores
#define PANEL_MIN_X  30  // Coordenada X para el panel de minutos
#define PANEL_SEC_X  170 // Coordenada X para el panel de segundos
#define PANEL_DEC_X  170 // Coordenada X para el panel de décimas (ajustado para 1 dígito)
#define PANEL_Y      50  // Coordenada Y común para los paneles
#define PANEL_Y_MIN  10  // Coordenada Y común para los paneles
#define PANEL_Y_DEC  110  // Coordenada Y común para los paneles

// Coordenadas X para los separadores (calculadas relativas a los paneles)
#define SEP1_X       160 //(PANEL_SEC_X - (DIGITO_ANCHO / 2) - 5) // X para los dos puntos ':'
#define SEP2_X       (PANEL_DEC_X - (DIGITO_ANCHO / 2) - 5) // X para el punto '.'

// Coordenadas Y para los separadores
#define SEP_Y1       45//(PANEL_Y + DIGITO_ALTO / 4)     // Y para punto superior de ':'
#define SEP_Y2       85//(PANEL_Y + 3 * DIGITO_ALTO / 4) // Y para punto inferior de ':'
#define SEP_Y_DEC    (PANEL_Y + DIGITO_ALTO / 2)     // Y para el punto decimal '.'
#define SEP_RADIUS   5                               // Radio de los círculos separadores

 // --- Configuración ---
 #define TAG "CRONOMETRO"

 // Definición de pines para LEDs
    #define LED_ROJO     GPIO_NUM_27
 //   #define LED_AMARILLO GPIO_NUM_25
    #define LED_VERDE    GPIO_NUM_26

 // Definición de pines para botones (configurados con pull-up)
 #define PB_Reset   GPIO_NUM_13
 //#define PB_Freeze  GPIO_NUM_12
 #define PB_Run_Stop  GPIO_NUM_14

// Tiempos
#define DEBOUNCE_TIME_MS       50  // Tiempo (ms) para estabilización del botón
#define BLINK_PERIOD_MS        500 // Periodo total (ON+OFF) del parpadeo del LED verde
#define TIMER_PERIOD_MS        100 // Periodo (ms) para incrementar décimas (0.1s resol.)

// Prioridades y Stack (Ajustar si es necesario)
#define TASK_PRIORITY_HIGH     5
#define TASK_PRIORITY_MEDIUM   4
#define TASK_PRIORITY_LOW      3
#define TASK_STACK_SIZE_MEDIUM 2048 // Stack para tareas de lógica simple
#define TASK_STACK_SIZE_LARGE  4096 // Stack mayor para tareas con más lógica o librerías (display)


// --- Variables Globales para la Pantalla (Punteros a los paneles de dígitos) ---
panel_t  panel_minutes = NULL;
panel_t  panel_seconds = NULL;
panel_t  panel_decimas = NULL;

// --- Variables Globales Compartidas ---
// volatile: indica al compilador que la variable puede cambiar externamente (otra tarea, timer)
volatile uint32_t decimas = 0;                  // Contador principal en décimas de segundo
volatile bool isRunning = false;                // Estado actual del cronómetro (corriendo/detenido)
volatile bool resetPressedWhileStopped = false; // Flag para indicar solicitud de reset Si esta en Stop

// Guardo los digitos de la pantalla
// Vector estático para almacenar los últimos dígitos dibujados
static uint8_t Digitos_Visualizados[] = { -1, -1, -1, -1, -1 };

// Handles para los Mutex
SemaphoreHandle_t xMutexPantalla = NULL; // Protege acceso a la pantalla (ILI9341, paneles)
SemaphoreHandle_t xMutexEstado = NULL;   // Protege el ESTADO compartido (decimas, isRunning, 
SemaphoreHandle_t xMutexLed = NULL;      // Protege el acceso a los LEDs


 // --- Prototipos de Funciones de Tareas y Callbacks ---
void tecladoTask(void * pvParameters);
void Manejo_LEDTask(void * pvParameters);
void displayTask(void * pvParameters);
void timerCallback(TimerHandle_t xTimer);

//--- Configuración de Pines GPIO ---
static void configure_gpios(void) {
    ESP_LOGI(TAG, "Configurando pines GPIO...");
    // Configurar Botones como Entrada con Pull-up interno habilitado
    gpio_config_t io_conf_button = {}; // Inicializar a cero
    io_conf_button.pin_bit_mask = (1ULL << PB_Run_Stop) | (1ULL << PB_Reset);
    io_conf_button.mode = GPIO_MODE_INPUT;
    io_conf_button.pull_up_en = GPIO_PULLUP_ENABLE; // Asume botones conectados a GND
    io_conf_button.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf_button.intr_type = GPIO_INTR_DISABLE; // Sin interrupciones, se usa polling
    esp_err_t err_btn = gpio_config(&io_conf_button);
    if (err_btn != ESP_OK)
        ESP_LOGE(TAG, "Error configurando botones: %s", esp_err_to_name(err_btn));

    // Configurar LEDs como Salida
    gpio_config_t io_conf_led = {}; // Inicializar a cero
    io_conf_led.pin_bit_mask = (1ULL << LED_VERDE) | (1ULL << LED_ROJO);
    io_conf_led.mode = GPIO_MODE_OUTPUT;
    io_conf_led.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf_led.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf_led.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err_led = gpio_config(&io_conf_led);
    if (err_led != ESP_OK)
        ESP_LOGE(TAG, "Error configurando LEDs: %s", esp_err_to_name(err_led));

    // Estado inicial de los LEDs (apagados)
    gpio_set_level(LED_VERDE, 0);
    gpio_set_level(LED_ROJO, 0); // Se encenderá en la tarea si está detenido al inicio

    ESP_LOGI(TAG, "GPIOs configurados (Start/Stop: %d, Reset: %d, Green: %d, Red: %d)", PB_Run_Stop,
             PB_Reset, LED_VERDE, LED_ROJO);
}

//--- Tarea para Gestión de Botones (Uso xMutexEstado) ---
void tecladoTask(void * pvParameters) {
    ESP_LOGI(TAG, "Inicio Tarea: tecladoTask");
    // Variables para debounce del botón Start/Stop
    int current_ss_state = 1;             // Estado actual leído (1=liberado, 0=presionado con pull-up)
    int last_steady_ss_state = 1;         // Último estado estable confirmado
    TickType_t last_debounce_ss_time = 0; // Tiempo (en ticks) del último rebote detectado

    // Variables para debounce del botón Reset
    int current_rst_state = 1;
    int last_steady_rst_state = 1;
    TickType_t last_debounce_rst_time = 0;

    while (1) {
        TickType_t now = xTaskGetTickCount(); // Tiempo actual en ticks

        // --- Lectura y Debounce Botón Start/Stop ---
        int leo_PB_Run_Stop = gpio_get_level(PB_Run_Stop);

        // Detectar cambio en la lectura (posible inicio de rebote o cambio real)
        if (leo_PB_Run_Stop != current_ss_state) {
            last_debounce_ss_time = now;   // Registrar el tiempo de este cambio
            current_ss_state = leo_PB_Run_Stop; // Actualizar el estado 'potencialmente inestable'
        }

        // Verificar si ha pasado suficiente tiempo desde el último cambio detectado (estabilización)
        if ((now - last_debounce_ss_time) > pdMS_TO_TICKS(DEBOUNCE_TIME_MS)) {
            // Si el estado actual (ya estable) es diferente del último estado estable confirmado...
            if (current_ss_state != last_steady_ss_state) {
                last_steady_ss_state = current_ss_state; // ...confirmar el nuevo estado estable.
                // Actuar solo en la transición a presionado (flanco descendente con pull-up)
                if (last_steady_ss_state == 0) {
                    ESP_LOGI(TAG, "[BTN] Start/Stop PRESIONADO");
                    // --- Sección Crítica: Modificar estado compartido (Uso xMutexEstado)
                    if (xSemaphoreTake(xMutexEstado, portMAX_DELAY) == pdTRUE) {
                        isRunning = !isRunning; // Invertir el estado de ejecución
                        ESP_LOGI(TAG, "[SYS] Cronómetro %s", isRunning ? "INICIADO" : "DETENIDO");
                        xSemaphoreGive(xMutexEstado); // Liberar Mutex
                    } else {
                         ESP_LOGE(TAG, "TecladoTask: Fallo al tomar Mutex de estado para Start/Stop!");
                    }
                    // --- Fin Sección Crítica ---
                } else {
                    ESP_LOGD(TAG, "[BTN] Start/Stop LIBERADO"); // Para depuración
                }
            }
        }

        // --- Lectura y Debounce Botón Reset ---
        // (Misma lógica que para Start/Stop)
        int leo_PB_Reset = gpio_get_level(PB_Reset);

        if (leo_PB_Reset != current_rst_state) {
            last_debounce_rst_time = now;
            current_rst_state = leo_PB_Reset;
        }

        if ((now - last_debounce_rst_time) > pdMS_TO_TICKS(DEBOUNCE_TIME_MS)) {
            if (current_rst_state != last_steady_rst_state) {
                last_steady_rst_state = current_rst_state;
                if (last_steady_rst_state == 0) { // Transición a presionado
                    ESP_LOGI(TAG, "[BTN] Reset PRESIONADO");
                    // --- Sección Crítica: Verificar estado y marcar para reset (Uso xMutexEstado)
                    if (xSemaphoreTake(xMutexEstado, portMAX_DELAY) == pdTRUE) {
                        // Leer isRunning dentro del mutex para asegurar consistencia
                       bool current_running_state = isRunning;
                       if (!current_running_state) { // Solo actuar si el cronómetro está DETENIDO
                           resetPressedWhileStopped = true; // Indicar a displayTask que resetee
                           ESP_LOGI(TAG, "[SYS] Reset solicitado (cronómetro detenido).");
                       } else {
                           ESP_LOGW(TAG, "[SYS] Reset ignorado (cronómetro corriendo).");
                       }
                       xSemaphoreGive(xMutexEstado); // Libero Mutex
                   } else {
                        ESP_LOGE(TAG, "TecladoTask: Fallo al tomar Mutex de estado para Reset!");
                   }
                    // --- Fin Sección Crítica ---
                } else {
                    ESP_LOGD(TAG, "[BTN] Reset LIBERADO"); // Para depuración
                }
            }
        }

        // Pausa breve para ceder tiempo de CPU a otras tareas
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

//--- Tarea para Gestión de LEDs Indicadores (Uso xMutexEstado)
void Manejo_LEDTask(void * pvParameters) {
    ESP_LOGI(TAG, "Inicio Tarea: Manejo_LEDTask");
    bool green_led_on = false;      // Estado actual del LED verde (para parpadeo)
    TickType_t last_blink_time = 0; // Momento del último cambio de estado del LED verde

    while (1) {
        bool current_status; // Variable local para guardar el estado leído

        // --- Sección Crítica: Leer estado compartido -Uso xMutexEstado
        if (xSemaphoreTake(xMutexEstado, portMAX_DELAY) == pdTRUE) {
            current_status = isRunning; // Leer el estado actual
            xSemaphoreGive(xMutexEstado);      // Liberar Mutex
        } else {
            ESP_LOGE(TAG, "LED Task: Fallo al tomar Mutex de estado!");
            vTaskDelay(pdMS_TO_TICKS(100)); // Esperar antes de reintentar
            continue;                       // Saltar el resto del ciclo
        }
        // --- Fin Sección Crítica ---

        TickType_t current_time = xTaskGetTickCount(); // Tiempo actual en ticks

        if (current_status) {
            // --- Cronómetro Corriendo: Parpadear LED Verde ---
            if (xSemaphoreTake(xMutexLed, portMAX_DELAY) == pdTRUE) {
                gpio_set_level(LED_ROJO, 0); // Aseguro que el LED Rojo esté apagado
                xSemaphoreGive(xMutexLed);
            } else {
                ESP_LOGE(TAG, "LED Task: Fallo al tomar Mutex de LED!");
            }


            // Lógica de parpadeo: cambiar estado cada (BLINK_PERIOD_MS / 2)
            if ((current_time - last_blink_time) >= pdMS_TO_TICKS(BLINK_PERIOD_MS / 2)) {
                if (xSemaphoreTake(xMutexLed, portMAX_DELAY) == pdTRUE) {
                    green_led_on = !green_led_on;             // Invertir estado
                    gpio_set_level(LED_VERDE, green_led_on);  // Aplicar nuevo estado al GPIO
                    xSemaphoreGive(xMutexLed);                // Libero Mutex 
                    last_blink_time = current_time;           // Registrar el tiempo del cambio DESPUÉS de dar el mutex
                } else {
                    ESP_LOGE(TAG, "LED Task: Fallo al tomar Mutex de LED para parpadeo!");
                }
            }
            // Pausa corta, necesaria para que el parpadeo sea visible y ceder CPU
            vTaskDelay(pdMS_TO_TICKS(50));

        } else {
            // --- Cronómetro Detenido: Encender LED Rojo fijo ---
            if (xSemaphoreTake(xMutexLed, portMAX_DELAY) == pdTRUE) {
                gpio_set_level(LED_VERDE, 0);   // Asegurar que el LED Verde esté apagado
                gpio_set_level(LED_ROJO, 1);    // Encender el LED Rojo
                xSemaphoreGive(xMutexLed);
            } else {
                ESP_LOGE(TAG, "LED Task: Fallo al tomar Mutex de LED para encender rojo!");
            }

            green_led_on = false;           // Resetear estado del LED verde para el próximo ciclo
            last_blink_time = current_time; // Resetear tiempo de parpadeo

            // Pausa más larga, ya que el estado es fijo
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

//--- Tarea para Actualizar Pantalla LCD (displayTask) ---

// Adaptada de tu ejemplo, con MUTEX y lógica de estado/reset
void displayTask(void * pvParameters) {
    ESP_LOGI(TAG, "Inicio Tarea: displayTask");
    uint32_t display_value_decimas = 0;  // Copia local del contador para mostrar
    bool perform_reset = false;          // Flag local para indicar si se debe resetear
    bool initial_draw_needed = true;     // Flag para realizar el primer dibujado (00:00.0)

    // Creación de Paneles (Sección Crítica Inicial de pantalla, Protejo solo con xMutexPantalla)
    if (xSemaphoreTake(xMutexPantalla, portMAX_DELAY) == pdTRUE) {
        ESP_LOGI(TAG, "Creando paneles de dígitos...");
        // Crea panel minutos (2 dígitos)
        panel_minutes = CrearPanel(PANEL_MIN_X + OFFSET_X, PANEL_Y_MIN, 2, DIGITO_ALTO, DIGITO_ANCHO, DIGITO_ENCENDIDO,
                                   DIGITO_APAGADO, DIGITO_FONDO);
        // Crea panel segundos (2 dígitos)
        panel_seconds = CrearPanel(PANEL_SEC_X + OFFSET_X, PANEL_Y_MIN, 2,DIGITO_ALTO, DIGITO_ANCHO, DIGITO_ENCENDIDO,
                                   DIGITO_APAGADO, DIGITO_FONDO);
        // Crea panel décimas (1 dígito visual)
        panel_decimas = CrearPanel(PANEL_DEC_X + OFFSET_X, PANEL_Y_DEC, 1,DIGITO_ALTO, DIGITO_ANCHO, DIGITO_ENCENDIDO,
                                   DIGITO_APAGADO, DIGITO_FONDO);

        if (!panel_minutes || !panel_seconds || !panel_decimas) {
            ESP_LOGE(TAG, "¡Error Crítico! No se pudieron crear los paneles de dígitos.");
            xSemaphoreGive(xMutexPantalla); // Libero Mutex
            vTaskDelete(NULL);              // Terminar esta tarea
            return;                         // Asegura que no continúe si vTaskDelete falla por alguna razón
        } else {
            ESP_LOGI(TAG, "Paneles creados exitosamente.");
        }
        xSemaphoreGive(xMutexPantalla); // Liberar Mutex después de crear paneles
    } else {
        ESP_LOGE(TAG, "DisplayTask: Fallo crítico al tomar Mutex de pantalla para crear paneles!");
        vTaskDelete(NULL); // Terminar esta tarea
        return;
    }
    // --- Fin Creación de Paneles ---

    // --- Bucle Principal de Actualización ---
    while (1) {
        // 1. Leer estado compartido y verificar reset (Sección Crítica de ESTADO)
        // Uso xMutexEstado
        if (xSemaphoreTake(xMutexEstado, portMAX_DELAY) == pdTRUE) {
            // Leer y potencialmente modificar variables de estado
            display_value_decimas = decimas; // Copiar valor actual
            perform_reset = resetPressedWhileStopped; // Copiar flag de reset

            if (perform_reset) {
                decimas = 0;                   // Resetear contador global
                display_value_decimas = 0;       // Actualizar copia local inmediato
                resetPressedWhileStopped = false; // Limpiar flag de solicitud global
                ESP_LOGI(TAG, "[DSP] Contador reseteado a 0 por solicitud.");
                initial_draw_needed = true; // Forzar redibujo completo a 00:00.0
            }
            xSemaphoreGive(xMutexEstado); // Liberar Mutex de Estado
        } else {
            ESP_LOGE(TAG, "DisplayTask: Fallo al tomar Mutex de estado para leer/resetear!");
            vTaskDelay(pdMS_TO_TICKS(50)); // Esperar y reintentar
            continue; // Saltar este ciclo de actualización
        }
        // --- Fin Sección Crítica (Lectura) ---

        // 2. Calcular valores MM:SS.D a partir de la copia local
        uint32_t total_tenths = display_value_decimas;
        uint32_t mins = (total_tenths / 10) / 60;   // Total segundos / 60
        uint32_t secs = (total_tenths / 10) % 60;   // Resto de segundos
        uint32_t d = total_tenths % 10;             // Resto de décimas
        uint8_t min_Decena = (mins / 10) % 10;      // Asegurar que no exceda 9
        uint8_t min_Unidad = mins % 10;
        uint8_t sec_Decena = secs / 10;
        uint8_t sec_Unidad = secs % 10;
        uint8_t Decima_Unidad = d;

        // 3. Dibujar en pantalla (Sección Crítica de PANTALLA)
        // Uso xMutexPantalla solo para las operaciones de dibujo
        if (xSemaphoreTake(xMutexPantalla, portMAX_DELAY) == pdTRUE) {

            // Realizar el dibujo inicial o si se reseteo
            if (initial_draw_needed) {
                // Limpiar/Dibujar todo a 0
                DibujarDigito(panel_minutes, 0, 0); Digitos_Visualizados[0] = 0;
                DibujarDigito(panel_minutes, 1, 0); Digitos_Visualizados[1] = 0;
                DibujarDigito(panel_seconds, 0, 0); Digitos_Visualizados[2] = 0;
                DibujarDigito(panel_seconds, 1, 0); Digitos_Visualizados[3] = 0;
                DibujarDigito(panel_decimas, 0, 0); Digitos_Visualizados[4] = 0;

                // Dibujar separadores (estado inicial apagado o como prefieras)
                ILI9341DrawFilledCircle(SEP1_X + OFFSET_X, SEP_Y1, SEP_RADIUS, DIGITO_ENCENDIDO); // O APAGADO? Decide el estado inicial
                ILI9341DrawFilledCircle(SEP1_X + OFFSET_X, SEP_Y2, SEP_RADIUS, DIGITO_ENCENDIDO); // O APAGADO?
                //ILI9341DrawFilledCircle(SEP2_X + OFFSET_X, SEP_Y_DEC, SEP_RADIUS, DIGITO_APAGADO); // Separador décimas si lo hubiera

                initial_draw_needed = false;
                ESP_LOGD(TAG, "[DSP] Dibujo inicial/reset realizado.");
            } else {
                 // Actualizar solo los dígitos que cambiaron
                 if (Digitos_Visualizados[0] != min_Decena) {
                    DibujarDigito(panel_minutes, 0, min_Decena);
                    Digitos_Visualizados[0] = min_Decena;
                 }
                 if (Digitos_Visualizados[1] != min_Unidad) {
                    DibujarDigito(panel_minutes, 1, min_Unidad);
                    Digitos_Visualizados[1] = min_Unidad;
                 }
                 if (Digitos_Visualizados[2] != sec_Decena) {
                    DibujarDigito(panel_seconds, 0, sec_Decena);
                    Digitos_Visualizados[2] = sec_Decena;
                 }
                 if (Digitos_Visualizados[3] != sec_Unidad) {
                    DibujarDigito(panel_seconds, 1, sec_Unidad);
                    Digitos_Visualizados[3] = sec_Unidad;
                 }

                 if (Digitos_Visualizados[4] != Decima_Unidad) {
                    // Log para depurar el error de timeout del Mutex. Corregido
                    // ESP_LOGD(TAG, "[DSP] Update Decima: %d -> %d", Digitos_Visualizados[4], Decima_Unidad); 
                    DibujarDigito(panel_decimas, 0, Decima_Unidad);
                    Digitos_Visualizados[4] = Decima_Unidad;
                 }
            }

             if (initial_draw_needed) { // Dibujar solo la primera vez o en reset
                 ILI9341DrawFilledCircle(SEP1_X + OFFSET_X, SEP_Y1, SEP_RADIUS, DIGITO_ENCENDIDO);
                 ILI9341DrawFilledCircle(SEP1_X + OFFSET_X, SEP_Y2, SEP_RADIUS, DIGITO_ENCENDIDO);
             }

            xSemaphoreGive(xMutexPantalla); // Liberar Mutex de Pantalla después de dibujar
        } else {
            ESP_LOGE(TAG, "DisplayTask: Fallo al tomar Mutex de pantalla para dibujar!");
        }
        // --- Fin Sección Crítica (Dibujo) ---

        // 4. Esperar antes de la próxima actualización
        // Sincronizar con la resolución del timer es una buena práctica
        // Actualizar un poco más rápido que el timer para asegurar que se vea el cambio de décima a tiempo.
        vTaskDelay(pdMS_TO_TICKS(TIMER_PERIOD_MS / 2));
    }
}


// Se ejecuta periódicamente para incrementar el contador
// Callback del Timer de FreeRTOS (Uso xMutexEstado)
void timerCallback(TimerHandle_t xTimer) {
    bool should_increment = false;
    // Intentar tomar el mutex de ESTADO (muy rápido)
    // CAMBIO: Usar xMutexEstado, no xMutexPantalla
    if (xSemaphoreTake(xMutexEstado, pdMS_TO_TICKS(5)) == pdTRUE) {
        // Leer isRunning DENTRO del mutex
        should_increment = isRunning;
        if (should_increment) { // Solo incrementar si el cronómetro está activo
            decimas++;
            // re pensar una lógica de overflow si es necesario (if -> decimas > MAX_VALUE)
        }
        xSemaphoreGive(xMutexEstado); // Liberar Mutex de Estado
    } else {
        ESP_LOGW(TAG, "Timer CB: Fallo al tomar xMutexEstado!");
    }
}

//--- Función Principal de la Aplicación (app_main) ---
void app_main(void) {
    ESP_LOGI(TAG, " === Inicio Aplicación Cronómetro FreeRTOS Curso ESE ===");

    // 1. Inicializar Hardware Básico
    configure_gpios();                  // Configurar pines para botones y LEDs
    ILI9341Init();                      // Inicializar controlador de pantalla y bus SPI
    ILI9341Rotate(ILI9341_Landscape_1); // Roto la pantalla
    ESP_LOGI(TAG, "Hardware Básico Inicializado (GPIOs, SPI, ILI9341).");

    // 2. Crear los Mutex (Fundamental para la sincronización)
    xMutexPantalla = xSemaphoreCreateMutex();
    if (xMutexPantalla == NULL) {
        ESP_LOGE(TAG, "¡Error Crítico! Creación de Mutex de pantalla fallida.");
        abort(); // Detener ejecución si el mutex no se puede crear
    }
    ESP_LOGI(TAG, "Mutex de pantalla creado correctamente.");

    xMutexEstado = xSemaphoreCreateMutex();
    if (xMutexEstado == NULL) {
        ESP_LOGE(TAG, "¡Error Crítico! Creación de Mutex de estado fallida.");
        vSemaphoreDelete(xMutexPantalla);
        abort();// Detener ejecución si el mutex no se puede crear
    }
    ESP_LOGI(TAG, "Mutex de teclado creado correctamente.");

    xMutexLed = xSemaphoreCreateMutex();
    if (xMutexLed == NULL) {
        ESP_LOGE(TAG, "¡Error Crítico! Creación de Mutex de LED fallida.");
        vSemaphoreDelete(xMutexPantalla);
        vSemaphoreDelete(xMutexEstado);
        abort(); // Detener ejecución si el mutex no se puede crear
    }
    ESP_LOGI(TAG, "Mutex de LED creado correctamente.");

    // 3. Crear las Tareas de la Aplicación
    ESP_LOGI(TAG, "Creando tareas...");
    BaseType_t task_status;
    task_status = xTaskCreate(tecladoTask, "TecladoTask", TASK_STACK_SIZE_MEDIUM, NULL, TASK_PRIORITY_MEDIUM, NULL);
    if (task_status != pdPASS) {
        ESP_LOGE(TAG, "Fallo al crear tecladoTask!");
        abort();
    }

    task_status = xTaskCreate(Manejo_LEDTask, "ManejoLEDTask", TASK_STACK_SIZE_MEDIUM, NULL, TASK_PRIORITY_LOW, NULL);
    if (task_status != pdPASS) {
        ESP_LOGE(TAG, "Fallo al crear Manejo_LEDTask!");
        abort();
    }

    task_status = xTaskCreate(displayTask, "DisplayTask", TASK_STACK_SIZE_LARGE, NULL, TASK_PRIORITY_MEDIUM, NULL);
    if (task_status != pdPASS) {
        ESP_LOGE(TAG, "Fallo al crear displayTask!");
        abort();
    }
    ESP_LOGI(TAG, "Tareas creadas.");

    // 4. Crear e Iniciar el Timer Periódico para el Cronómetro
    ESP_LOGI(TAG, "Creo e inicio el timer del cronómetro...");
    TimerHandle_t xStopwatchTimer = xTimerCreate("CronometroTimer",              // Nombre para debugging
                                                 pdMS_TO_TICKS(TIMER_PERIOD_MS), // Periodo en ticks
                                                 pdTRUE,       // Auto-recarga habilitada (es periódico)
                                                 (void *)0,    // ID del timer (no se usa aquí)
                                                 timerCallback // Función a llamar cada periodo
    );

    if (xStopwatchTimer == NULL) {
        ESP_LOGE(TAG, "¡Error Crítico! Creación del Timer fallida.");
        abort(); // Detener si el timer no se puede crear
    } else {
        if (xTimerStart(xStopwatchTimer, 0) != pdPASS) {
            ESP_LOGE(TAG, "¡Error Crítico! No se pudo iniciar el Timer.");
            abort(); // Detener si el timer no arranca
        } else {
            ESP_LOGI(TAG, "Timer del cronómetro iniciado (Periodo: %d ms).", TIMER_PERIOD_MS);
        }
    }

    ESP_LOGI(TAG, "=== Sistema Inicializado y Corriendo ===");
    // app_main puede terminar aquí, FreeRTOS se encarga de ejecutar las tareas y timers.
}