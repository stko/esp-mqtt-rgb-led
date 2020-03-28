#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <Arduino.h> // needed for bool declaraion

typedef uint8_t byte;
typedef struct
{
    byte RGB[3];
} moviePixel;

typedef  moviePixel moviePixelArray[];


typedef struct
{
  unsigned long transitionTime;
  char  * movieName;
  bool loop;
  bool hardChange;
  moviePixelArray * moviePixels;
  size_t size;
} movieStruct;

extern const moviePixel sunrise[];
extern size_t mySunriseSize;
extern const movieStruct movieArray[];
extern size_t movieArraySize;

