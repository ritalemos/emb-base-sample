#ifndef MERCOSUL_PLATE_H
#define MERCOSUL_PLATE_H

#include <stdbool.h>

/**
 * Validates Mercosul license plate format.
 * Supports: Brazil, Argentina, Venezuela, Uruguay, Paraguay, Bolivia.
 * Input can contain spaces, hyphens, and is case-insensitive.
 */
bool mercosul_plate_is_valid(const char *raw);

/**
 * Returns country identification for the plate format.
 */
const char *mercosul_plate_get_country(const char *raw);

#endif /* MERCOSUL_PLATE_H */