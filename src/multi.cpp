#include "multi/multi.h"

namespace multi
{
	static Context STATIC_CONTEXT;
	Context*& context()
	{
		static Context* currentContext = &STATIC_CONTEXT;
		return currentContext;
	}
}