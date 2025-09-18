// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "NodeGraphSignature.h"
#include "Assets/DepVal.h"
#include "Assets/ChunkFileContainer.h"

namespace Assets { class CompilerRegistration; class IIntermediateCompilers; }

namespace ShaderSourceParser
{
	class SignatureAsset
	{
	public:
		const GraphLanguage::ShaderFragmentSignature& GetSignature() const;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		SignatureAsset(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal);
		~SignatureAsset();

		static const ::Assets::ArtifactRequest ChunkRequests[1];
	private:
		std::unique_ptr<uint8_t[], PODAlignedDeletor>	_rawMemoryBlock;
		size_t											_rawMemoryBlockSize = 0;
		::Assets::DependencyValidation					_depVal;
	};

	constexpr auto GetCompileProcessType(SignatureAsset*) { return ConstHash64("shader-signature"); }

	::Assets::CompilerRegistration RegisterSignatureAssetCompiler(::Assets::IIntermediateCompilers&);
}

