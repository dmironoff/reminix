//
// Created by dmironov on 19.03.2026.
//

/*
 * У меня в libc нет функции strtoul
 * Поэтому есть быстрая реализация из интернетов
 * Ну да.... такой я ленивый=)
 */

static unsigned int char_to_val(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }
    return 16; // Invalid digit
}

unsigned long strtoul(const char* nptr, char** endptr, int base)
{
    const char *p = nptr;
    unsigned long result = 0;
    unsigned int value;
    int digit_seen = 0;

    // 1. Skip leading whitespace (using simple space/tab check for bare-metal)
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') {
        p++;
    }

    // 2. Handle base 0 (auto-detect base 8, 10, or 16 based on prefix)
    if (base == 0) {
        if (*p == '0') {
            p++;
            if (*p == 'x' || *p == 'X') {
                p++;
                base = 16;
            } else {
                base = 8; // Octal if it starts with 0
            }
        } else {
            base = 10; // Decimal by default
        }
    }

    // 3. Process digits
    while ((value = char_to_val(*p)) < base) {
        result = result * base + value;
        digit_seen = 1;
        p++;
    }

    // 4. Set endptr if requested
    if (endptr != 0) {
        // If no digits were seen, endptr should point back to the original string
        *endptr = (char *)(digit_seen ? p : nptr);
    }

    // This basic implementation does not include robust overflow checking
    // as required by the full C standard for simplicity in a bare-metal context.
    return result;
}