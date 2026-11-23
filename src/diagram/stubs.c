/* The diagram types of 6d-iii are not built yet: they are recognised and reported. */
#include "diagram_internal.h"

int dg_class(ExDiagram *d, DgLines *L) { (void)L; dg_fail(d, "class diagrams are not built yet"); return -1; }
int dg_state(ExDiagram *d, DgLines *L, const char *f, size_t n) { (void)L; (void)f; (void)n; dg_fail(d, "state diagrams are not built yet"); return -1; }
int dg_er(ExDiagram *d, DgLines *L) { (void)L; dg_fail(d, "entity-relationship diagrams are not built yet"); return -1; }
int dg_pie(ExDiagram *d, DgLines *L, const char *f, size_t n) { (void)L; (void)f; (void)n; dg_fail(d, "pie charts are not built yet"); return -1; }
int dg_gantt(ExDiagram *d, DgLines *L) { (void)L; dg_fail(d, "gantt charts are not built yet"); return -1; }
