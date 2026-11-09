/* The same output as main.cpp through the C interface, compiled as C */
#include <stdio.h>

#include "vcd_writer_c.h"

#define BITS 8

/* *value* as BITS binary digits */
static const char* bits(int value, char out[BITS + 1])
{
    int i;
    for (i = 0; i < BITS; ++i)
        out[i] = (value >> (BITS - 1 - i)) & 1 ? '1' : '0';
    out[BITS] = '\0';
    return out;
}

int main(void)
{
    vcd_writer *writer = NULL;
    vcd_var *counter_var = NULL, *var_var = NULL;
    char value[BITS + 1];
    vcd_status status = vcd_open(&writer, "dump_c.vcd", vcd_timescale_one, vcd_unit_ns, "2024-01-15 19:16:21", NULL, NULL, 0);
    uint64_t timestamp;

    if (status == vcd_ok)
        status = vcd_register_var(writer, "a.b.c", "counter", vcd_var_integer, BITS, NULL, &counter_var);
    if (status == vcd_ok)
        status = vcd_register_var(writer, "a.b", "var", vcd_var_integer, BITS, NULL, &var_var);
    for (timestamp = 0; timestamp < 5 && status == vcd_ok; ++timestamp)
    {
        status = vcd_change(writer, counter_var, timestamp, bits(10 + (int)timestamp * 2, value), NULL);
        if (status == vcd_ok)
            status = vcd_change(writer, var_var, timestamp, bits(11 + (int)timestamp * 2, value), NULL);
    }
    if (status == vcd_ok)
    {
        status = vcd_close(writer, NULL);
        writer = NULL;
    }
    if (status != vcd_ok)
        printf("%s\n", vcd_last_error());
    vcd_close(writer, NULL); /* after an error */
    return status;
}
