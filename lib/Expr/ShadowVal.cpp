#include "ShadowVal.h"

using namespace klee;

namespace klee
{
template<> ShadowValU64::svcache_ty	ShadowValU64::cache = ShadowValU64::svcache_ty();
template<> ShadowValExpr::svcache_ty	ShadowValExpr::cache = ShadowValExpr::svcache_ty();
}
