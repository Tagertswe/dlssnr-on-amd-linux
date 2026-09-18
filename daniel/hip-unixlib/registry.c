#include "registry.h"

#include <string.h>

const void *resolve_bundle_ptr(const void *data)
{
    static const char magic[] = "__CLANG_OFFLOAD_BUNDLE__";
    if (memcmp(data, magic, sizeof(magic) - 1) == 0)
        return data;

    const void *inner = *(const void * const *)((const char *)data + 8);
    if (inner && memcmp(inner, magic, sizeof(magic) - 1) == 0)
        return inner;

    return NULL;
}

#define MAX_FATBINS 16
#define MAX_FUNCTIONS 256
#define MAX_FUNCTION_NAME 127
#define MAX_VARS 32

struct fatbin_entry { const void *token; void *module; };
struct function_entry { const void *host_fn; void *function; char name[MAX_FUNCTION_NAME + 1]; };
struct var_entry { const void *host_var; void *device_ptr; };

static struct fatbin_entry fatbins[MAX_FATBINS];
static int fatbin_count;
static struct function_entry functions[MAX_FUNCTIONS];
static int function_count;
static struct var_entry vars[MAX_VARS];
static int var_count;

void registry_reset(void)
{
    fatbin_count = 0;
    function_count = 0;
    var_count = 0;
}

int registry_add_fatbin(const void *token, void *module)
{
    if (fatbin_count >= MAX_FATBINS)
        return 0;
    fatbins[fatbin_count].token = token;
    fatbins[fatbin_count].module = module;
    fatbin_count++;
    return 1;
}

void *registry_find_module(const void *token)
{
    for (int i = 0; i < fatbin_count; i++)
        if (fatbins[i].token == token)
            return fatbins[i].module;
    return NULL;
}

int registry_add_function(const void *host_fn, void *function, const char *name)
{
    if (function_count >= MAX_FUNCTIONS)
        return 0;
    functions[function_count].host_fn = host_fn;
    functions[function_count].function = function;
    if (name)
    {
        size_t i = 0;
        for (; name[i] && i < MAX_FUNCTION_NAME; i++)
            functions[function_count].name[i] = name[i];
        functions[function_count].name[i] = '\0';
    }
    else
    {
        functions[function_count].name[0] = '\0';
    }
    function_count++;
    return 1;
}

void *registry_find_function(const void *host_fn)
{
    for (int i = 0; i < function_count; i++)
        if (functions[i].host_fn == host_fn)
            return functions[i].function;
    return NULL;
}

const char *registry_find_function_name(const void *host_fn)
{
    for (int i = 0; i < function_count; i++)
        if (functions[i].host_fn == host_fn)
            return functions[i].name;
    return NULL;
}

int registry_add_var(const void *host_var, void *device_ptr)
{
    if (var_count >= MAX_VARS)
        return 0;
    vars[var_count].host_var = host_var;
    vars[var_count].device_ptr = device_ptr;
    var_count++;
    return 1;
}

void *registry_find_var(const void *host_var)
{
    for (int i = 0; i < var_count; i++)
        if (vars[i].host_var == host_var)
            return vars[i].device_ptr;
    return NULL;
}
