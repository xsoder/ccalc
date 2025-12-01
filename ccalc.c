#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <string.h>

#define INIT_SIZE 3

struct Number {
    int *items;
    size_t count;
    size_t size;
};

struct Op {
    char *items;
    size_t count;
    size_t size;
};

void da_append(struct Number *nums, int item)
{
    if (!nums->items) {
        nums->size = INIT_SIZE;
        nums->count = 0;
        nums->items = malloc(nums->size * sizeof(item));
    }
    if (nums->count > nums->size) {
        nums->size *= 2;
        nums->items = realloc(nums->items, nums->size * sizeof(item));
    }
    nums->items[nums->count++] = item;
}

void op_append(struct Op *op, char item)
{
    if (!op->items) {
        op->size = INIT_SIZE;
        op->count = 0;
        op->items = malloc(op->size * sizeof(item));
    }
    if (op->count > op->size) {
        op->size *= 2;
        op->items = realloc(op->items, op->size * sizeof(item));
    }
    op->items[op->count++] = item;
}

bool is_digit(char c)
{
    return (c>='0' && c <= '9');
}

bool is_op(char c)
{
    switch(c) {
        case '+': return true;
        case '-': return true;
        case '*': return true;
        case '/': return true;
    }
    return false;
}

void evaluate(struct Number *nums, struct Op *op)
{
    int result = 0;
    for (size_t i = 0; i < nums->size; i++) {
        if (op->items[i] == '+') result = nums->items[i] + nums->items[i+1];
        else if (op->items[i] == '-') result = nums->items[i] - nums->items[i+1];
        else if (op->items[i] == '*') result = nums->items[i] * nums->items[i+1];
        else if (op->items[i] == '/') result = nums->items[i] * nums->items[i+1];
    }
    printf("%d\n", result);
    nums->count = 0;
    op->count = 0;
}

int main(void)
{
    struct Number nums = {0};
    struct Op op = {0};
    while(true){
        char *input = readline("> ");
        if(!input) {
            perror("[ERROR]: Readline input error");
        }
        if (strcmp(input, "exit") == 0) exit(0);
        for (int i = 0; i < strlen(input); ++i) {
            char c = input[i];
            if (c == '\n') break;
            if (c == ' ' || c == '\t' || c == '\r') continue;
            bool ok = is_digit(c);
            if (ok) {
                da_append(&nums, c - '0');
                continue;
            }
            ok = is_op(c);
            if (ok) {
                op_append(&op, c);
                continue;
            }
        }

        evaluate(&nums, &op);
    }

    return 0;
}
