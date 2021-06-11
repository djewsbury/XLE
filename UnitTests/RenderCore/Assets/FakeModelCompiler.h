// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../../Assets/IntermediateCompilers.h"

namespace UnitTests
{
	::Assets::CompilerRegistration RegisterFakeModelCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers);
}