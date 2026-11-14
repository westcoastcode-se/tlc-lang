#include <stddef.h>
#include <stdint.h>
typedef int32_t (*cb_t)(int32_t);
static cb_t saved;
void set_cb(cb_t f) { saved = f; }
cb_t get_cb(void) { return saved; }
int32_t run_cb(cb_t f, int32_t v) { return f == NULL ? -1 : f(v); }
typedef uint8_t (*narrow_t)(uint8_t);
int32_t run_narrow(narrow_t f) { return f == NULL ? -1 : (int32_t)f(200); }
struct Hooks { cb_t on; int32_t tag; };
int32_t run_hooks(struct Hooks *h) { return h->on == NULL ? h->tag : h->on(h->tag); }
void clear_hooks(struct Hooks *h) { h->on = NULL; }
