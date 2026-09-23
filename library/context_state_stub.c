#include "context_access.h"

/* Stub-owned context symbol.  A non-NULL sentinel means that a fake context
 * has been created; no code ever dereferences it. */
GLcontext mini_CurrentContext = (GLcontext)0;
