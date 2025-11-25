#ifndef VALIDATE_H
#define VALIDATE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Validates Mercosul license plate format.
 * Supports: Brazil, Argentina, Venezuela, Uruguay, Paraguay, Bolivia.
 * Input can contain spaces, hyphens, and is case-insensitive.
 */
bool plate_is_valid(const char *raw);

/**
 * Returns country identification for the plate format.
 * Useful for debugging and reporting.
 */
const char *plate_get_country(const char *raw);

#ifdef __cplusplus
}
#endif

#endif /* VALIDATE_H */