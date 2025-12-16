// Compile with cc -std=c99 -o ccalc ccalc.c -lm

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>

#define INIT_SIZE 8

typedef long long bigint;

typedef enum {
    BASE_DEC = 10,
    BASE_HEX = 16,
    BASE_BIN = 2,
    BASE_OCT = 8
} NumberBase;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_SQRT, OP_NONE
} Operation;

struct BigNumber {
    bigint *items;
    size_t count;
    size_t capacity;
};

struct OpArray {
    Operation *items;
    size_t count;
    size_t capacity;
};

void bn_init(struct BigNumber *bn) {
    bn->capacity = INIT_SIZE;
    bn->count = 0;
    bn->items = malloc(sizeof(bigint) * bn->capacity);
}

void bn_append(struct BigNumber *bn, bigint num) {
    if (bn->count >= bn->capacity) {
        bn->capacity *= 2;
        bn->items = realloc(bn->items, sizeof(bigint) * bn->capacity);
    }
    bn->items[bn->count++] = num;
}

void bn_clear(struct BigNumber *bn) {
    bn->count = 0;
}

void op_init(struct OpArray *op) {
    op->capacity = INIT_SIZE;
    op->count = 0;
    op->items = malloc(sizeof(Operation) * op->capacity);
}

void op_append(struct OpArray *op, Operation item) {
    if (op->count >= op->capacity) {
        op->capacity *= 2;
        op->items = realloc(op->items, sizeof(Operation) * op->capacity);
    }
    op->items[op->count++] = item;
}

void op_clear(struct OpArray *op) {
    op->count = 0;
}


void print_in_base(bigint num, NumberBase base) {
    switch (base) {
        case BASE_HEX:
            printf("0x%llx\n", num);
            break;
        case BASE_OCT:
            printf("0o%llo\n", num);
            break;
        case BASE_BIN: {
            printf("0b");
            bool started = false;
            for (int i = 63; i >= 0; i--) {
                int bit = (num >> i) & 1;
                if (bit) started = true;
                if (started) putchar(bit ? '1' : '0');
            }
            if (!started) putchar('0');
            putchar('\n');
            break;
        }
        default:
            printf("%lld\n", num);
    }
}

bool parse_number(const char *str, bigint *result, size_t *consumed) {
    char *end = NULL;

    if (strncmp(str, "0x", 2) == 0 || strncmp(str, "0X", 2) == 0) {
        *result = strtoll(str + 2, &end, 16);
    } else if (strncmp(str, "0b", 2) == 0 || strncmp(str, "0B", 2) == 0) {
        *result = strtoll(str + 2, &end, 2);
    } else if (strncmp(str, "0o", 2) == 0 || strncmp(str, "0O", 2) == 0) {
        *result = strtoll(str + 2, &end, 8);
    } else if (isdigit(*str)) {
        *result = strtoll(str, &end, 10);
    } else {
        return false;
    }

    *consumed = (size_t)(end - str);
    return true;
}

Operation parse_operator(const char *str, size_t *consumed) {
    if (strncmp(str, "pow", 3) == 0) {
        *consumed = 3;
        return OP_POW;
    } else if (strncmp(str, "**", 2) == 0) {
        *consumed = 2;
        return OP_POW;
    } else if (strncmp(str, "sqrt", 4) == 0) {
        *consumed = 4;
        return OP_SQRT;
    } else if (str[0] == '^') {
        *consumed = 1;
        return OP_POW;
    } else if (str[0] == '+') {
        *consumed = 1;
        return OP_ADD;
    } else if (str[0] == '-') {
        *consumed = 1;
        return OP_SUB;
    } else if (str[0] == '*') {
        *consumed = 1;
        return OP_MUL;
    } else if (str[0] == '/') {
        *consumed = 1;
        return OP_DIV;
    }

    *consumed = 0;
    return OP_NONE;
}

void evaluate(struct BigNumber *nums, struct OpArray *ops,
              NumberBase output_base) {
    if (nums->count == 0) return;

    bigint result = nums->items[0];

    for (size_t i = 0; i < ops->count; i++) {
        if (ops->items[i] != OP_SQRT && i + 1 >= nums->count) {
            fprintf(stderr, "ERROR: Missing operand\n");
            return;
        }

        switch (ops->items[i]) {
            case OP_ADD:
                result += nums->items[i + 1];
                break;
            case OP_SUB:
                result -= nums->items[i + 1];
                break;
            case OP_MUL:
                result *= nums->items[i + 1];
                break;
            case OP_DIV:
                if (nums->items[i + 1] == 0) {
                    fprintf(stderr, "ERROR: Division by zero\n");
                    return;
                }
                result /= nums->items[i + 1];
                break;
            case OP_POW: {
                bigint exp = nums->items[i + 1];
                if (exp < 0) {
                    fprintf(stderr, "ERROR: Negative exponent\n");
                    return;
                }
                bigint r = 1;
                for (bigint j = 0; j < exp; j++) r *= result;
                result = r;
                break;
            }
            case OP_SQRT:
                if (result < 0) {
                    fprintf(stderr, "ERROR: Negative square root\n");
                    return;
                }
                result = (bigint)sqrt((double)result);
                break;
            default:
                break;
        }
    }

    printf("Result: ");
    print_in_base(result, output_base);
}

void print_help(void) {
    printf("\nCalculator REPL\n");
    printf("Operators: +  -  *  /  ^  **  pow  sqrt\n");
    printf("Number formats: decimal, 0x(hex), 0b(bin), 0o(oct)\n");
    printf("Commands:\n");
    printf("  :hex  :bin  :dec  :oct   set output base\n");
    printf("  :help                    show help\n");
    printf("  :exit / quit              leave REPL\n\n");
}

int main(void) {
    struct BigNumber nums;
    struct OpArray ops;

    bn_init(&nums);
    op_init(&ops);

    NumberBase output_base = BASE_DEC;

    printf("Type ':help' for commands\n\n");

    char buf[1024];

    while (true) {
        printf("λ ");
        fflush(stdout);

        if (!fgets(buf, sizeof(buf), stdin))
            break;

        buf[strcspn(buf, "\n")] = 0;

        if (!strcmp(buf, ":exit") || !strcmp(buf, ":quit"))
            break;

        if (!strcmp(buf, ":help")) {
            print_help();
            continue;
        }
        if (!strcmp(buf, ":hex")) { output_base = BASE_HEX; continue; }
        if (!strcmp(buf, ":bin")) { output_base = BASE_BIN; continue; }
        if (!strcmp(buf, ":oct")) { output_base = BASE_OCT; continue; }
        if (!strcmp(buf, ":dec")) { output_base = BASE_DEC; continue; }

        bn_clear(&nums);
        op_clear(&ops);

        size_t i = 0;
        size_t len = strlen(buf);

        while (i < len) {
            if (isspace(buf[i])) {
                i++;
                continue;
            }

            bigint num;
            size_t consumed;

            if (parse_number(buf + i, &num, &consumed)) {
                bn_append(&nums, num);
                i += consumed;
            } else {
                Operation op = parse_operator(buf + i, &consumed);
                if (op == OP_NONE) {
                    fprintf(stderr, "ERROR: Invalid character '%c'\n", buf[i]);
                    break;
                }
                op_append(&ops, op);
                i += consumed;
            }
        }

        evaluate(&nums, &ops, output_base);
    }

    free(nums.items);
    free(ops.items);
    return 0;
}
