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

#ifndef DIGITOS_H_
#define DIGITOS_H_

/** @file digitos.h
 ** @brief Declaraciones de la biblioteca para dibujar paneles con digitos de 7 segmentos en una pantalla TFT
 **/

/* === Headers files inclusions ==================================================================================== */

#include <stdint.h>

/* === Cabecera C++ ================================================================================================ */

#ifdef __cplusplus
extern "C" {
#endif

/* === Public macros definitions =================================================================================== */

//! @brief Cantidad máxima de paneles que se pueden crear
#ifndef MAXIMO_PANELES
#define MAXIMO_PANELES 4
#endif

//! @brief Cantidad máxima de digitos que se pueden mostrar en un panel
#ifndef MAXIMO_DIGITOS
#define MAXIMO_DIGITOS 6
#endif

/* === Public data type declarations =============================================================================== */

//! @brief Tipo de dato para referenciar a un panel de digitos
typedef struct panel_s * panel_t;

/* === Public variable declarations ================================================================================ */

/* === Public function declarations ================================================================================ */

/**
 * @brief Función que crea un panel de n digitos de 7 segmentos en una pantalla TFT
 *
 * @param  x         Posición horizontal de la esquina superior derecha del panel
 * @param  y         Posición vertical de la esquina superior derecha del panel
 * @param  digitos   Cantidad de digitos que se pueden mostrar en el panel
 * @param  alto      Alto en pixeles del caracter del panel
 * @param  ancho     Ancho en pixeles del caracter del panel
 * @param  encendido Color de los segmentos encendidos de los digitos
 * @param  apagado   Color de los segmentos apagados de los digitos
 * @param  fondo     Color de fondo del panel
 * @return panel_t   Puntero al panel creado
 */
panel_t CrearPanel(uint16_t x, uint16_t y, uint16_t digitos, uint16_t alto, uint16_t ancho, uint16_t encendido,
                   uint16_t apagado, uint16_t fondo);

/**
 * @brief Función para actualizar el valor de un digito en un panel
 *
 * @param self       Puntero al panel creado con la funcion @ref CrearPanel
 * @param posicion   Posición del digito que se desea actualizar
 * @param valor      Valor que se desea mostrar en el digito
 */
void DibujarDigito(panel_t self, uint8_t posicion, uint8_t valor);

/* === End of documentation ======================================================================================== */

#ifdef __cplusplus
}
#endif

#endif /* DIGITOS_H_ */
