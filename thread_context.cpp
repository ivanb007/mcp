#include "thread_context.h"

thread_local ThreadContext g_ctx; // default TT size; can be re-inited per thread if desired
