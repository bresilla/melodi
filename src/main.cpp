#include "modem.h"

static Modem modem;

void setup()
{
    modem.begin();
}

void loop()
{
    modem.poll();
}
