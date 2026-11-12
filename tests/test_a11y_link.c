/* a11y_link: the AccessKit static library built from the vendored crates links
 * into a C program and its C API works. (The accessibility wrapper and its tests
 * come in 1b-ii and 1b-iii.) */
#include <stdio.h>
#include <string.h>

#include "accesskit.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void) {
    struct accesskit_node *button = accesskit_node_new(ACCESSKIT_ROLE_BUTTON);
    struct accesskit_node *window = accesskit_node_new(ACCESSKIT_ROLE_WINDOW);
    CHECK(button != NULL && window != NULL);
    accesskit_node_set_label(button, "OK");
    accesskit_node_set_label(window, "Exhaust");
    accesskit_node_free(button);
    accesskit_node_free(window);
    return failures ? 1 : 0;
}
