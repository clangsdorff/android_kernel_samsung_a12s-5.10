// The xcover5 Keymaster libs were built against a BoringSSL that still exported the sk_* stack
// functions; the GSI libcrypto only has the OPENSSL_sk_* names, so the old ones forward to them.
#include <stddef.h>

typedef struct stack_st OPENSSL_STACK;
typedef void (*sk_free_func)(void *);
typedef void (*sk_call_free_func)(sk_free_func, void *);

size_t OPENSSL_sk_num(const OPENSSL_STACK *sk);
void *OPENSSL_sk_value(const OPENSSL_STACK *sk, size_t i);
OPENSSL_STACK *OPENSSL_sk_new_null(void);
size_t OPENSSL_sk_push(OPENSSL_STACK *sk, void *p);
void *OPENSSL_sk_delete(OPENSSL_STACK *sk, size_t where);
void OPENSSL_sk_pop_free_ex(OPENSSL_STACK *sk, sk_call_free_func call_free, sk_free_func free_func);

#define EXPORT __attribute__((visibility("default")))

EXPORT size_t sk_num(const OPENSSL_STACK *sk) { return OPENSSL_sk_num(sk); }
EXPORT void *sk_value(const OPENSSL_STACK *sk, size_t i) { return OPENSSL_sk_value(sk, i); }
EXPORT OPENSSL_STACK *sk_new_null(void) { return OPENSSL_sk_new_null(); }
EXPORT size_t sk_push(OPENSSL_STACK *sk, void *p) { return OPENSSL_sk_push(sk, p); }
EXPORT void *sk_delete(OPENSSL_STACK *sk, size_t where) { return OPENSSL_sk_delete(sk, where); }

EXPORT void sk_pop_free_ex(OPENSSL_STACK *sk, sk_call_free_func call_free, sk_free_func free_func)
{
    OPENSSL_sk_pop_free_ex(sk, call_free, free_func);
}

static void call_free(sk_free_func free_func, void *p) { free_func(p); }

EXPORT void sk_pop_free(OPENSSL_STACK *sk, sk_free_func free_func)
{
    OPENSSL_sk_pop_free_ex(sk, call_free, free_func);
}
