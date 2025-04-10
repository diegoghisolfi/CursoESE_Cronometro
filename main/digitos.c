/*********************************************************************************************************************
Copyright (c) 2025, Esteban Volentini <evolentini@herrera.unt.edu.ar>

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

SPDX-License-Identifier: MIT
*********************************************************************************************************************/

/** @file digitos.c
 ** @brief Definiciones de la biblioteca para dibujar paneles con digitos de 7 segmentos en una pantalla TFT
 **/

/* === Headers files inclusions ==================================================================================== */

#include "digitos.h"
#include "ili9341.h"
#include <stddef.h>

/* === Macros definitions ========================================================================================== */

#define SEGMENTO_A 0x01 //!< Máscara para el segmento A
#define SEGMENTO_B 0x02 //!< Máscara para el segmento B
#define SEGMENTO_C 0x04 //!< Máscara para el segmento C
#define SEGMENTO_D 0x08 //!< Máscara para el segmento D
#define SEGMENTO_E 0x10 //!< Máscara para el segmento E
#define SEGMENTO_F 0x20 //!< Máscara para el segmento F
#define SEGMENTO_G 0x40 //!< Máscara para el segmento G

/* === Private data type declarations ============================================================================== */

typedef struct punto_s {
    uint16_t x;
    uint16_t y;
} * punto_t;

typedef struct area_s {
    struct punto_s desde;
    struct punto_s hasta;
} * area_t;

typedef struct segmentos_s {
    struct area_s a;
    struct area_s b;
    struct area_s c;
    struct area_s d;
    struct area_s e;
    struct area_s f;
    struct area_s g;
} * segmentos_t;

struct panel_s {
    struct punto_s origen;
    uint16_t digitos;
    uint16_t ancho;
    uint16_t alto;
    uint16_t encendido;
    uint16_t apagado;
    uint16_t fondo;
    struct segmentos_s segmentos;
    uint8_t valores[MAXIMO_DIGITOS];
};

/* === Private variable declarations =============================================================================== */

static const uint8_t DIGITOS[] = {
    0x3F, 0x06, 0x5B, 0x4F, // 0,1,2,3
    0x66, 0x6D, 0x7D, 0x07, // 4,5,6,7
    0x7F, 0x6F, 0x77, 0x7C, // 8,9,A,B
    0x39, 0x5E, 0x79, 0x71, // C,D,E,F
    0x00,
};

/* === Private function declarations =============================================================================== */

/* === Public variable definitions ================================================================================= */

/* === Private variable definitions ================================================================================ */

/* === Private function definitions ================================================================================ */

panel_t CrearInstancia(void) {
    static struct panel_s instancias[MAXIMO_PANELES];

    for (int indice = 0; indice < MAXIMO_PANELES; indice++) {
        if (instancias[indice].digitos == 0) {
            return &(instancias[indice]);
        }
    }
    return NULL;
}

void CalcularGeometria(panel_t self) {

    if (self->ancho == 0) {
        self->ancho = (self->alto * 60) / 100;
    }

    uint16_t ancho_barra = (self->alto * 7) / 100;
    uint16_t margen = (ancho_barra * 75) / 100;
    uint16_t separacion = (self->alto * 2) / 100;

    segmentos_t s = &(self->segmentos);

    s->a.desde.x = s->d.desde.x = s->g.desde.x = margen + ancho_barra + separacion;
    s->a.hasta.x = s->d.hasta.x = s->g.hasta.x = self->ancho - (margen + ancho_barra + separacion);
    s->a.hasta.y = margen + ancho_barra;
    s->g.desde.y = (self->alto - ancho_barra) / 2;
    s->g.hasta.y = (self->alto + ancho_barra) / 2;
    s->d.desde.y = self->alto - (margen + ancho_barra);

    s->a.desde.y = s->b.desde.y = s->f.desde.y = margen;
    s->b.hasta.y = s->f.hasta.y = (self->alto - separacion) / 2;
    s->c.desde.y = s->e.desde.y = (self->alto + separacion) / 2;
    s->d.hasta.y = s->c.hasta.y = s->e.hasta.y = self->alto - margen;

    s->e.desde.x = s->f.desde.x = margen;
    s->e.hasta.x = s->f.hasta.x = margen + ancho_barra;
    s->b.desde.x = s->c.desde.x = self->ancho - margen;
    s->b.hasta.x = s->c.hasta.x = self->ancho - (margen + ancho_barra);
}

void BorrarDigito(panel_t self, uint8_t digito) {
    struct area_s area;

    area.desde.x = self->origen.x + digito * self->ancho;
    area.desde.y = self->origen.y;
    area.hasta.x = self->origen.x + (digito + 1) * self->ancho;
    area.hasta.y = self->origen.y + self->alto;

    ILI9341DrawFilledRectangle(area.desde.x, area.desde.y, area.hasta.x, area.hasta.y, self->fondo);
}

void DibujarSegmento(panel_t self, uint8_t digito, area_t segmento, uint16_t color) {
    struct area_s area;

    area.desde.x = self->origen.x + digito * self->ancho + segmento->desde.x;
    area.desde.y = self->origen.y + segmento->desde.y;
    area.hasta.x = self->origen.x + digito * self->ancho + segmento->hasta.x;
    area.hasta.y = self->origen.y + segmento->hasta.y;

    ILI9341DrawFilledRectangle(area.desde.x, area.desde.y, area.hasta.x, area.hasta.y, color);
}

/* === Public function implementation ============================================================================== */

panel_t CrearPanel(uint16_t x, uint16_t y, uint16_t digitos, uint16_t alto, uint16_t ancho, uint16_t encendido,
                   uint16_t apagado, uint16_t fondo) {
    panel_t self = CrearInstancia();
    if (self) {
        self->origen.x = x;
        self->origen.y = y;
        self->alto = alto;
        self->ancho = ancho;

        if (digitos > MAXIMO_DIGITOS) {
            self->digitos = MAXIMO_DIGITOS;
        } else if (digitos < 1) {
            self->digitos = 1;
        } else {
            self->digitos = digitos;
        }

        self->encendido = encendido;
        self->apagado = apagado;
        self->fondo = fondo;

        CalcularGeometria(self);
    }

    for (int i = 0; i < self->digitos; i++) {
        DibujarDigito(self, i, 0xFF);
    }
    return self;
}

void DibujarDigito(panel_t self, uint8_t posicion, uint8_t valor) {
    if (posicion < self->digitos) {
        uint8_t segmentos;

        self->valores[posicion] = valor;
        if (valor > sizeof(DIGITOS)) {
            self->valores[posicion] = sizeof(DIGITOS);
        }
        segmentos = DIGITOS[self->valores[posicion]];

        BorrarDigito(self, posicion);
        DibujarSegmento(self, posicion, &(self->segmentos.a), segmentos & SEGMENTO_A ? self->encendido : self->apagado);
        DibujarSegmento(self, posicion, &(self->segmentos.b), segmentos & SEGMENTO_B ? self->encendido : self->apagado);
        DibujarSegmento(self, posicion, &(self->segmentos.c), segmentos & SEGMENTO_C ? self->encendido : self->apagado);
        DibujarSegmento(self, posicion, &(self->segmentos.d), segmentos & SEGMENTO_D ? self->encendido : self->apagado);
        DibujarSegmento(self, posicion, &(self->segmentos.e), segmentos & SEGMENTO_E ? self->encendido : self->apagado);
        DibujarSegmento(self, posicion, &(self->segmentos.f), segmentos & SEGMENTO_F ? self->encendido : self->apagado);
        DibujarSegmento(self, posicion, &(self->segmentos.g), segmentos & SEGMENTO_G ? self->encendido : self->apagado);
    }
}

/* === End of documentation ======================================================================================== */
