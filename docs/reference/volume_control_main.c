#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define CHIP "gpiochip0"
#define PIN_SUBIR 17
#define PIN_BAJAR 27

/* Variables globales para poder liberarlas desde el manejador de señal */
static struct gpiod_chip *chip = NULL;
static struct gpiod_line *b_subir = NULL;
static struct gpiod_line *b_bajar = NULL;
static volatile sig_atomic_t salir = 0;

/* Manejador de Ctrl+C (SIGINT) y SIGTERM */
static void manejar_senal(int sig) {
    (void)sig;
    salir = 1;
}

/* Ejecuta amixer sin pasar por /bin/sh (más rápido y seguro) */
static void ejecutar_amixer(const char *direccion) {
    pid_t pid = fork();
    if (pid == 0) {
        /* Proceso hijo: reemplaza su imagen por amixer */
        execlp("amixer", "amixer",
               "-D", "pulse",
               "sset", "Master", direccion,
               (char *)NULL);
        _exit(127);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    }
}

int main(void) {
    /* 1) Instalar manejadores de señal para limpieza ordenada */
    struct sigaction sa = {0};
    sa.sa_handler = manejar_senal;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* 2) Abrir el chip GPIO */
    chip = gpiod_chip_open_by_name(CHIP);
    if (!chip) {
        perror("gpiod_chip_open_by_name");
        return 1;
    }

    /* 3) Obtener las dos líneas y comprobar que existan */
    b_subir = gpiod_chip_get_line(chip, PIN_SUBIR);
    b_bajar = gpiod_chip_get_line(chip, PIN_BAJAR);
    if (!b_subir || !b_bajar) {
        perror("gpiod_chip_get_line");
        gpiod_chip_close(chip);
        return 1;
    }

    /* 4) Configurar la request CON el pull-up */
    struct gpiod_line_request_config cfg = {
        .consumer     = "vol",
        .request_type = GPIOD_LINE_REQUEST_EVENT_BOTH_EDGES,
        .flags        = GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP
    };

    if (gpiod_line_request(b_subir, &cfg, 0) < 0) {
        perror("request b_subir");
        gpiod_chip_close(chip);
        return 1;
    }
    if (gpiod_line_request(b_bajar, &cfg, 0) < 0) {
        perror("request b_bajar");
        gpiod_line_release(b_subir);
        gpiod_chip_close(chip);
        return 1;
    }

    /* 5) Antirrebote del kernel (10 ms). */
    gpiod_line_set_debounce_period_us(b_subir, 10000);
    gpiod_line_set_debounce_period_us(b_bajar, 10000);

    /* 6) Agrupar las dos líneas en un bulk para esperar por ambas a la vez */
    struct gpiod_line_bulk bulk;
    gpiod_line_bulk_init(&bulk);
    gpiod_line_bulk_add(&bulk, b_subir);
    gpiod_line_bulk_add(&bulk, b_bajar);

    struct gpiod_line_bulk eventos;

    printf("Control de volumen activo. SUBIR=GPIO%d  BAJAR=GPIO%d\n",
           PIN_SUBIR, PIN_BAJAR);
    printf("Ctrl+C para salir.\n");

    /* 7) Bucle principal: espera bloqueante, 0% de CPU */
    while (!salir) {
        int ret = gpiod_line_event_wait_bulk(&bulk, NULL, &eventos);
        if (ret < 0) {
            /* EINTR = interrumpido por una señal (Ctrl+C) */
            if (salir) break;
            perror("event_wait_bulk");
            continue;
        }
        if (ret == 0) continue;

        unsigned n = gpiod_line_bulk_num_lines(&eventos);
        for (unsigned i = 0; i < n; i++) {
            struct gpiod_line *linea = gpiod_line_bulk_get_line(&eventos, i);
            struct gpiod_line_event ev;

            /* ¡Obligatorio! Leer el evento para vaciar la cola del kernel */
            if (gpiod_line_event_read(linea, &ev) < 0) {
                perror("event_read");
                continue;
            }

            /* Solo actuamos en FALLING_EDGE = botón presionado */
            if (ev.event_type != GPIOD_LINE_EVENT_FALLING_EDGE)
                continue;

            int pin = gpiod_line_offset(linea);
            if (pin == PIN_SUBIR) {
                printf("SUBIR\n");
                ejecutar_amixer("5%+");
            } else if (pin == PIN_BAJAR) {
                printf("BAJAR\n");
                ejecutar_amixer("5%-");
            }
        }
    }

    /* 8) Limpieza ordenada */
    printf("\nSaliendo...\n");
    gpiod_line_release(b_subir);
    gpiod_line_release(b_bajar);
    gpiod_chip_close(chip);
    return 0;
}
