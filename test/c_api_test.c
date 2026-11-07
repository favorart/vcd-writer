/* Test of the C interface, compiled as C.
 * Usage: c_api_test <reference compare.vcd> */
#include <stdio.h>
#include <string.h>

#include "vcd_writer_c.h"

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { ++failures; printf("%s:%d: CHECK(%s) failed: %s\n", __FILE__, __LINE__, #cond, vcd_last_error()); } } while (0)

/* *value* as 8 binary digits */
static void bits8(int value, char out[9])
{
    int i;
    for (i = 0; i < 8; ++i)
        out[i] = (value & (1 << (7 - i))) ? '1' : '0';
    out[8] = '\0';
}

/* Contents of two files are equal */
static int same_files(const char *a, const char *b)
{
    FILE *fa = fopen(a, "rb");
    FILE *fb = fopen(b, "rb");
    int ca = 0, cb = 0, same = (fa != NULL && fb != NULL);
    while (same)
    {
        ca = fgetc(fa);
        cb = fgetc(fb);
        same = (ca == cb);
        if (ca == EOF || cb == EOF)
            break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return same;
}

/* The same output as test/main.cpp */
static void test_compare(const char *reference)
{
    vcd_writer *writer = NULL;
    vcd_var *counter = NULL, *var = NULL;
    char value[9];
    int t, changed = 0;

    CHECK(vcd_open(&writer, "c_dump.vcd", vcd_timescale_one, vcd_unit_ns, "2024-01-15 19:16:21", NULL, NULL, 0) == vcd_ok);
    CHECK(vcd_register_var(writer, "a.b.c", "counter", vcd_var_integer, 8, NULL, &counter) == vcd_ok);
    CHECK(vcd_register_var(writer, "a.b", "var", vcd_var_integer, 8, NULL, &var) == vcd_ok);
    for (t = 0; t < 5; ++t)
    {
        bits8(10 + t * 2, value);
        CHECK(vcd_change(writer, counter, (uint64_t)t, value, &changed) == vcd_ok);
        CHECK(changed == 1);
        bits8(11 + t * 2, value);
        CHECK(vcd_change_by_name(writer, "a.b", "var", (uint64_t)t, value, NULL) == vcd_ok);
    }
    bits8(10 + 4 * 2, value); /* the same value is not written */
    CHECK(vcd_change(writer, counter, 4, value, &changed) == vcd_ok);
    CHECK(changed == 0);
    CHECK(vcd_close(writer, NULL) == vcd_ok);
    CHECK(same_files("c_dump.vcd", reference));
}

/* Errors are statuses with a message */
static void test_errors(void)
{
    vcd_writer *writer = NULL;
    vcd_var *var = NULL;

    CHECK(vcd_open(NULL, "c_err.vcd", vcd_timescale_one, vcd_unit_ns, NULL, NULL, NULL, 0) == vcd_error_argument);
    CHECK(vcd_open(&writer, "c_err.vcd", (vcd_timescale)5, vcd_unit_ns, NULL, NULL, NULL, 0) == vcd_error_type);
    CHECK(writer == NULL);
    CHECK(strlen(vcd_last_error()) > 0);
    CHECK(vcd_open(&writer, "c_err.vcd", vcd_timescale_one, vcd_unit_ns, "today", NULL, NULL, 0) == vcd_error_type);
    CHECK(vcd_open(&writer, "no_such_dir/c_err.vcd", vcd_timescale_one, vcd_unit_ns, NULL, NULL, NULL, 0) == vcd_error_io);

    CHECK(vcd_open(&writer, "c_err.vcd", vcd_timescale_ten, vcd_unit_us, "", "comment", "version", 0) == vcd_ok);
    CHECK(strcmp(vcd_last_error(), "") == 0);
    CHECK(vcd_register_var(writer, "s", "bad name", vcd_var_wire, 1, NULL, &var) == vcd_error_type);
    CHECK(var == NULL);
    CHECK(vcd_register_var(writer, "s", "v", (vcd_var_type)vcd_var_count_, 1, NULL, &var) == vcd_error_type);
    CHECK(vcd_register_var(writer, "s", "v", vcd_var_wire, 0, NULL, &var) == vcd_error_type);
    CHECK(vcd_register_var(writer, "s", "v", vcd_var_wire, 4, "1010", &var) == vcd_ok);
    CHECK(vcd_set_scope_type(writer, "s", (vcd_scope_type)vcd_scope_count_) == vcd_error_type);
    CHECK(vcd_set_scope_type(writer, "none", vcd_scope_task) == vcd_error_phase);
    CHECK(vcd_set_scope_type(writer, "s", vcd_scope_task) == vcd_ok);
    CHECK(vcd_change(writer, var, 5, "10a0", NULL) == vcd_error_type);
    CHECK(vcd_change(writer, var, 5, "0101", NULL) == vcd_ok);
    CHECK(vcd_change(writer, var, 4, "1111", NULL) == vcd_error_phase);
    CHECK(vcd_change(writer, NULL, 6, "1111", NULL) == vcd_error_argument);
    CHECK(vcd_change_by_name(writer, "s", "none", 6, "1", NULL) == vcd_error_phase);
    CHECK(vcd_register_var(writer, "s", "w", vcd_var_wire, 1, NULL, &var) == vcd_error_phase);
    CHECK(vcd_dump_off(writer, 7) == vcd_ok);
    CHECK(vcd_dump_on(writer, 8) == vcd_ok);
    CHECK(vcd_flush(writer, NULL) == vcd_ok);
    CHECK(vcd_close(writer, NULL) == vcd_ok);
    CHECK(vcd_close(NULL, NULL) == vcd_ok);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: %s <compare.vcd>\n", argv[0]);
        return 2;
    }
    test_compare(argv[1]);
    test_errors();
    if (failures)
    {
        printf("FAILED: %d checks\n", failures);
        return 1;
    }
    printf("PASSED\n");
    return 0;
}
