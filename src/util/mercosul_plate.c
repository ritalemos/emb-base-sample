#include "mercosul_plate.h"
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */
#define MERCOSUL_PLATE_LEN  7
#define CLEAN_BUFFER_SIZE   16

/* ============================================================================
 * CHARACTER CLASSIFICATION
 * ============================================================================ */

static inline bool char_is_alpha(char c)
{
    c = (char)toupper((unsigned char)c);
    return (c >= 'A' && c <= 'Z');
}

static inline bool char_is_numeric(char c)
{
    return (c >= '0' && c <= '9');
}

static inline bool char_is_separator(char c)
{
    return (c == ' ' || c == '-' || c == '\t');
}

/* ============================================================================
 * PLATE PATTERN VALIDATORS
 * ============================================================================ */

/* Brazil: ABC1D23 */
static bool validate_format_br(const char *p)
{
    return char_is_alpha(p[0]) &&
           char_is_alpha(p[1]) &&
           char_is_alpha(p[2]) &&
           char_is_numeric(p[3]) &&
           char_is_alpha(p[4]) &&
           char_is_numeric(p[5]) &&
           char_is_numeric(p[6]);
}

/* Argentina/Venezuela: AB123CD */
static bool validate_format_ar_ve(const char *p)
{
    return char_is_alpha(p[0]) &&
           char_is_alpha(p[1]) &&
           char_is_numeric(p[2]) &&
           char_is_numeric(p[3]) &&
           char_is_numeric(p[4]) &&
           char_is_alpha(p[5]) &&
           char_is_alpha(p[6]);
}

/* Uruguay: ABC1234 */
static bool validate_format_uy(const char *p)
{
    return char_is_alpha(p[0]) &&
           char_is_alpha(p[1]) &&
           char_is_alpha(p[2]) &&
           char_is_numeric(p[3]) &&
           char_is_numeric(p[4]) &&
           char_is_numeric(p[5]) &&
           char_is_numeric(p[6]);
}

/* Paraguay auto: ABCD123 */
static bool validate_format_py_car(const char *p)
{
    return char_is_alpha(p[0]) &&
           char_is_alpha(p[1]) &&
           char_is_alpha(p[2]) &&
           char_is_alpha(p[3]) &&
           char_is_numeric(p[4]) &&
           char_is_numeric(p[5]) &&
           char_is_numeric(p[6]);
}

/* Paraguay moto: 123ABCD */
static bool validate_format_py_moto(const char *p)
{
    return char_is_numeric(p[0]) &&
           char_is_numeric(p[1]) &&
           char_is_numeric(p[2]) &&
           char_is_alpha(p[3]) &&
           char_is_alpha(p[4]) &&
           char_is_alpha(p[5]) &&
           char_is_alpha(p[6]);
}

/* Bolivia cargo: AB12345 */
static bool validate_format_bo(const char *p)
{
    return char_is_alpha(p[0]) &&
           char_is_alpha(p[1]) &&
           char_is_numeric(p[2]) &&
           char_is_numeric(p[3]) &&
           char_is_numeric(p[4]) &&
           char_is_numeric(p[5]) &&
           char_is_numeric(p[6]);
}

/* ============================================================================
 * PLATE NORMALIZATION
 * ============================================================================ */

static bool sanitize_plate_string(const char *input, char *output, size_t out_size)
{
    if (!input || !output || out_size == 0) {
        return false;
    }

    size_t pos = 0;

    for (size_t i = 0; input[i] != '\0' && pos < out_size - 1; i++) {
        char ch = input[i];

        if (char_is_separator(ch)) {
            continue;
        }

        if (isalpha((unsigned char)ch) || isdigit((unsigned char)ch)) {
            output[pos++] = (char)toupper((unsigned char)ch);
        } else {
            return false;
        }
    }

    output[pos] = '\0';
    return (pos == MERCOSUL_PLATE_LEN);
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

bool mercosul_plate_is_valid(const char *raw)
{
    if (!raw) {
        return false;
    }

    char clean[CLEAN_BUFFER_SIZE];
    
    if (!sanitize_plate_string(raw, clean, sizeof(clean))) {
        return false;
    }

    return validate_format_br(clean)      ||
           validate_format_ar_ve(clean)   ||
           validate_format_uy(clean)      ||
           validate_format_py_car(clean)  ||
           validate_format_py_moto(clean) ||
           validate_format_bo(clean);
}

const char *mercosul_plate_get_country(const char *raw)
{
    if (!raw) {
        return "INVALID";
    }

    char clean[CLEAN_BUFFER_SIZE];
    
    if (!sanitize_plate_string(raw, clean, sizeof(clean))) {
        return "INVALID";
    }

    if (validate_format_br(clean))      return "BRAZIL";
    if (validate_format_ar_ve(clean))   return "ARGENTINA/VENEZUELA";
    if (validate_format_uy(clean))      return "URUGUAY";
    if (validate_format_py_car(clean))  return "PARAGUAY (AUTO)";
    if (validate_format_py_moto(clean)) return "PARAGUAY (MOTO)";
    if (validate_format_bo(clean))      return "BOLIVIA (CARGO)";

    return "UNKNOWN";
}