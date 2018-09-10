#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>

typedef uint8_t byte;
typedef struct
{
    byte RGB[3];
} sunriseItem;

extern const sunriseItem sunrise[];
extern size_t mySunriseSize;
