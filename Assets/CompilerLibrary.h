// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ICompileOperation.h"
#include "IntermediateCompilers.h"
#include "../Assets/AssetUtils.h"
#include "../Utility/StringUtils.h"
#include <vector>

namespace Assets
{
 	// Compiler shared libraries -- 

	class ICompilerDesc
	{
	public:
		virtual std::string			Description() const = 0;

		class FileKind
		{
		public:
			IteratorRange<const ArtifactTargetCode*> _targetCodes;
			std::string		_regexFilter = {};
			std::string		_name = {};
			std::string		_shortName = {};
			std::string		_extensionsForOpenDlg = {};		// comma separated list of extensions for file-open-dialog scenarios
		};
		virtual unsigned			FileKindCount() const = 0;
		virtual FileKind			GetFileKind(unsigned index) const = 0;

		virtual ~ICompilerDesc();
	};

	using GetCompilerDescFn = std::shared_ptr<ICompilerDesc>();   
}