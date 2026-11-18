#include <gtk/gtk.h>
#include <libpq-fe.h>
#include <stdio.h>

int main(void) {
    printf("GTK runtime: %u.%u.%u; libpq runtime: %d\n",
           gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
           PQlibVersion());
    /* No display, database server, or credentials required for this link check. */
    return 0;
}
